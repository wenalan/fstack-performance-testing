// server_kernel.cpp
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <vector>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"

constexpr int LISTEN_PORT = 8080;
constexpr int BACKLOG = 1024;

static bool recv_all_bytes(int fd, char* buffer, size_t len)
{
    size_t received = 0;
    while (received < len) {
        ssize_t n = recv(fd, buffer + received, len - received, 0);
        if (n == 0) {
            return false;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv");
            return false;
        }

        received += static_cast<size_t>(n);
    }

    return true;
}

static bool send_all_bytes(int fd, const char* buffer, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buffer + sent, len - sent, 0);
        if (n == 0) {
            return false;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("send");
            return false;
        }

        sent += static_cast<size_t>(n);
    }

    return true;
}

static bool recv_full_msg(int fd, std::vector<char>& buffer)
{
    Msg header{};
    if (!recv_all_bytes(fd, reinterpret_cast<char*>(&header), sizeof(header))) {
        return false;
    }

    if (header.payload_size < sizeof(Msg)) {
        std::fprintf(stderr,
                     "invalid payload_size=%" PRIu32 " (< header size %zu)\n",
                     header.payload_size, sizeof(Msg));
        return false;
    }

    const size_t payload_bytes = header.payload_size - sizeof(Msg);
    buffer.resize(header.payload_size);
    std::memcpy(buffer.data(), &header, sizeof(header));

    if (payload_bytes > 0 &&
        !recv_all_bytes(fd, buffer.data() + sizeof(Msg), payload_bytes)) {
        return false;
    }

    return true;
}

static bool send_full_msg(int fd, const std::vector<char>& buffer)
{
    return send_all_bytes(fd, buffer.data(), buffer.size());
}

static void handle_conn(int fd) {
    std::vector<char> buffer;
    for (;;) {
        if (!recv_full_msg(fd, buffer))
            break;
        if (!send_full_msg(fd, buffer))
            break;
    }

    close(fd);
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    const int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(LISTEN_PORT);

    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen");
        return 1;
    }

    printf("Kernel echo server listening on port %d\n", LISTEN_PORT);
    printf("Minimum total message size: %zu bytes\n", sizeof(Msg));

    for (;;) {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        int conn_fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&cliaddr),
                             &clilen);
        if (conn_fd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }

        handle_conn(conn_fd);
    }

    close(listen_fd);
    return 0;
}
