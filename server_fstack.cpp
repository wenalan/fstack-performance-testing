// server_fstack.cpp
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"
#include <ff_api.h>

constexpr int LISTEN_PORT = 8080;
constexpr int BACKLOG = 1024;
constexpr int MAX_CLIENTS = 1024;

// Must match [port0].addr in config.ini
//static const char *g_bind_ip = "192.168.5.220";

static int g_listenfd = -1;

// Per-connection state
struct ClientState {
    int fd = -1;
    std::vector<char> recv_buffer;
    size_t recv_bytes = 0;
    size_t expected_size = sizeof(Msg);
    std::vector<char> send_buffer;
    size_t send_bytes = 0;
    bool has_full_msg = false;
};

static std::array<ClientState, MAX_CLIENTS> g_client_states{};
static int g_client_count = 0;

// Helper: remove fd from the array
static void remove_client_at(int idx)
{
    if (idx < 0 || idx >= g_client_count)
        return;

    ClientState& state = g_client_states[idx];
    if (state.fd >= 0) {
        ff_close(state.fd);
    }

    // Replace removed entry with the last one
    if (idx < g_client_count - 1) {
        g_client_states[idx] = std::move(g_client_states[g_client_count - 1]);
    }
    // Reset the final slot state
    g_client_states[g_client_count - 1] = ClientState{};
    g_client_count--;
}

// Receive a complete message (non-blocking)
// Returns: -1=error, 0=need more data, 1=got full message
static int recv_message(ClientState& state, int idx)
{
    // Already have a full message buffered
    if (state.has_full_msg) {
        return 1;
    }

    if (state.recv_buffer.size() < state.expected_size) {
        state.recv_buffer.resize(state.expected_size);
    }

    while (state.recv_bytes < state.expected_size) {
        ssize_t n = ff_recv(state.fd,
                            state.recv_buffer.data() + state.recv_bytes,
                            state.expected_size - state.recv_bytes,
                            0);

        if (n > 0) {
            state.recv_bytes += n;

            if (state.recv_bytes == sizeof(Msg) &&
                state.expected_size == sizeof(Msg)) {
                auto* header = reinterpret_cast<Msg*>(state.recv_buffer.data());
                if (header->payload_size < sizeof(Msg)) {
                    std::fprintf(stderr,
                                 "client fd=%d payload_size=%" PRIu32 " below header size %zu\n",
                                 state.fd, header->payload_size, sizeof(Msg));
                    remove_client_at(idx);
                    return -1;
                }

                state.expected_size = header->payload_size;
                state.recv_buffer.resize(state.expected_size);
            }
        } else if (n == 0) {
            std::fprintf(stderr, "client fd=%d closed (recv)\n", state.fd);
            remove_client_at(idx);
            return -1;
        } else {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EPERM) {
                return 0;
            }
            perror("ff_recv");
            remove_client_at(idx);
            return -1;
        }
    }

    state.send_buffer = state.recv_buffer;
    state.send_bytes = 0;
    state.has_full_msg = true;

    state.recv_buffer.assign(sizeof(Msg), 0);
    state.expected_size = sizeof(Msg);
    state.recv_bytes = 0;

    return 1;
}

// Send a complete message (non-blocking)
// Returns: -1=error, 0=in progress, 1=done
static int send_message(ClientState& state, int idx)
{
    if (!state.has_full_msg) {
        return 1;  // Nothing to send
    }

    if (state.send_buffer.empty()) {
        state.has_full_msg = false;
        return 1;
    }

    while (state.send_bytes < state.send_buffer.size()) {
        ssize_t n = ff_send(state.fd,
                           state.send_buffer.data() + state.send_bytes,
                           state.send_buffer.size() - state.send_bytes,
                           0);
        
        if (n > 0) {
            state.send_bytes += n;
        } else if (n == 0) {
            // Peer closed the connection
            std::fprintf(stderr, "client fd=%d closed (send)\n", state.fd);
            remove_client_at(idx);
            return -1;
        } else {
            // n < 0
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EPERM) {
                // Can't send now; retry on next loop
                return 0;
            }
            perror("ff_send");
            remove_client_at(idx);
            return -1;
        }
    }
    
    // Send complete; reset state
    state.send_bytes = 0;
    state.has_full_msg = false;
    state.send_buffer.clear();
    
    return 1;
}

// Run one non-blocking recv+echo attempt for a single client
static void process_one_client(int idx)
{
    ClientState& state = g_client_states[idx];
    
    // 1. Attempt to receive a complete message
    int recv_result = recv_message(state, idx);
    if (recv_result < 0) {
        return;  // Error or need more data
    }
    
    // 2. Attempt to send the message
    int send_result = send_message(state, idx);
    if (send_result < 0) {
        return;  // Error
    }
    
    // Continue next loop if still sending
}

static int server_loop(void *arg)
{
    (void)arg;

    // Initialize the listen fd on first call
    if (g_listenfd < 0) {
        g_listenfd = ff_socket(AF_INET, SOCK_STREAM, 0);
        if (g_listenfd < 0) {
            perror("ff_socket");
            return -1;
        }

        const int yes = 1;
        if (ff_setsockopt(g_listenfd, SOL_SOCKET, SO_REUSEADDR,
                          &yes, sizeof(yes)) < 0) {
            perror("ff_setsockopt");
            return -1;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(LISTEN_PORT);

        // if (inet_pton(AF_INET, g_bind_ip, &addr.sin_addr) != 1) {
        //     perror("inet_pton g_bind_ip");
        //     return -1;
        // }
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (ff_bind(g_listenfd,
                    reinterpret_cast<struct linux_sockaddr *>(&addr),
                    sizeof(addr)) < 0) {
            perror("ff_bind");
            return -1;
        }

        if (ff_listen(g_listenfd, BACKLOG) < 0) {
            perror("ff_listen");
            return -1;
        }

        std::printf("F-Stack simple echo server listening on %s:%d\n",
                    g_bind_ip, LISTEN_PORT);
        std::fprintf(stdout, "Msg header size: %zu bytes\n", sizeof(Msg));
    }

    // 1) Accept as many new connections as possible per loop
    for (;;) {
        int cfd = ff_accept(g_listenfd, nullptr, nullptr);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EINTR || errno == EPERM) {
                // No more pending connections
                break;
            }
            perror("ff_accept");
            break;
        }

        if (g_client_count >= MAX_CLIENTS) {
            std::fprintf(stderr, "too many clients, closing fd=%d\n", cfd);
            ff_close(cfd);
            continue;
        }

        // Initialize new client state
        ClientState& state = g_client_states[g_client_count];
        state.fd = cfd;
        state.recv_bytes = 0;
        state.expected_size = sizeof(Msg);
        state.recv_buffer.assign(sizeof(Msg), 0);
        state.send_bytes = 0;
        state.send_buffer.clear();
        state.has_full_msg = false;
        
        g_client_count++;
        // printf("new client fd=%d, total=%d\n", cfd, g_client_count);
    }

    // 2) Walk every connected client in this loop
    int idx = 0;
    while (idx < g_client_count) {
        int current_client_count = g_client_count;  // Save current value
        
        process_one_client(idx);
        
        // If a client was removed, g_client_count shrinks
        // If the current client remains, move to the next one
        if (g_client_count == current_client_count) {
            // Client not removed
            idx++;
        }
        // If a client was removed, idx stays because next client moved here
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int ret = ff_init(argc, argv);
    if (ret < 0) {
        std::fprintf(stderr, "ff_init failed\n");
        return 1;
    }

    ff_run(server_loop, nullptr);
    return 0;
}
