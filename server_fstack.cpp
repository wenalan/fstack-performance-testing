// server_fstack_simple_fixed.cpp
// 修复了partial receive问题的简单版本

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"
#include <ff_api.h>

constexpr int LISTEN_PORT = 8080;
constexpr int BACKLOG = 1024;
constexpr int MAX_CLIENTS = 1024;

// 跟 config.ini 里的 [port0].addr 保持一致
static const char *g_bind_ip = "192.168.5.220";

static int g_listenfd = -1;

// 每个连接的状态
struct ClientState {
    int fd = -1;
    Msg recv_msg;                // 正在接收的消息
    size_t recv_bytes = 0;       // 已接收的字节数
    Msg send_msg;                // 要发送的消息
    size_t send_bytes = 0;       // 已发送的字节数
    bool has_full_msg = false;   // 是否有完整的消息待发送
};

static std::array<ClientState, MAX_CLIENTS> g_client_states{};
static int g_client_count = 0;

// 辅助：把 fd 从数组中删除
static void remove_client_at(int idx)
{
    if (idx < 0 || idx >= g_client_count)
        return;

    ClientState& state = g_client_states[idx];
    if (state.fd >= 0) {
        ff_close(state.fd);
    }

    // 用最后一个覆盖当前
    if (idx < g_client_count - 1) {
        g_client_states[idx] = std::move(g_client_states[g_client_count - 1]);
    }
    // 重置最后一个位置的状态
    g_client_states[g_client_count - 1] = ClientState{};
    g_client_count--;
}

// 接收完整消息（非阻塞）
// 返回: -1=出错, 0=需要更多数据, 1=收到完整消息
static int recv_message(ClientState& state, int idx)
{
    // 如果已经有完整消息，先发送
    if (state.has_full_msg) {
	    perror("full msg");
        return 1;
    }
    
    // 继续接收剩余的数据
    char* buffer = reinterpret_cast<char*>(&state.recv_msg);
    
    while (state.recv_bytes < sizeof(Msg)) {
	    printf("recv bytes: %d\n", (int)state.recv_bytes);
        ssize_t n = ff_recv(state.fd, 
                           buffer + state.recv_bytes,
                           sizeof(Msg) - state.recv_bytes, 
                           0);
	    printf("n: %d\n", (int)n);
        
        if (n > 0) {
            state.recv_bytes += n;
        } else if (n == 0) {
            // 对端正常关闭
            std::fprintf(stderr, "client fd=%d closed (recv)\n", state.fd);
            remove_client_at(idx);
            return -1;
        } else {
            // n < 0
            if (errno == EINTR) {
                // 信号打断，继续尝试
                continue;
            }
            if (errno == EAGAIN || errno == EPERM) {
                // 没有数据了，下次再试
                return 0;
            }
            // 其它错误
            perror("ff_recv");
            remove_client_at(idx);
            return -1;
        }
    }
    
    perror("got msg");
    // 收到完整消息
    state.has_full_msg = true;
    return 1;
}

// 发送完整消息（非阻塞）
// 返回: -1=出错, 0=发送中, 1=发送完成
static int send_message(ClientState& state, int idx)
{
    if (!state.has_full_msg) {
        return 1;  // 没有消息要发送
    }
    
    const char* buffer = reinterpret_cast<const char*>(&state.recv_msg);
    
    while (state.send_bytes < sizeof(Msg)) {
        ssize_t n = ff_send(state.fd,
                           buffer + state.send_bytes,
                           sizeof(Msg) - state.send_bytes,
                           0);
        
        if (n > 0) {
            state.send_bytes += n;
        } else if (n == 0) {
            // 对端关闭
            std::fprintf(stderr, "client fd=%d closed (send)\n", state.fd);
            remove_client_at(idx);
            return -1;
        } else {
            // n < 0
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EPERM) {
                // 不能再发了，下次继续
                return 0;
            }
            perror("ff_send");
            remove_client_at(idx);
            return -1;
        }
    }
    
    // 发送完成，重置状态
    state.recv_bytes = 0;
    state.send_bytes = 0;
    state.has_full_msg = false;
    
    return 1;
}

// 尝试对单个 client 做一次 non-blocking recv + echo
static void process_one_client(int idx)
{
    ClientState& state = g_client_states[idx];
    
    // 1. 尝试接收完整消息
    int recv_result = recv_message(state, idx);
    if (recv_result < 0) {
	    perror("recv error");
        return;  // 出错或需要更多数据
    }
    
    // 2. 尝试发送消息
    int send_result = send_message(state, idx);
    if (send_result < 0) {
	    perror("send error");
        return;  // 出错
    }
    
    // 如果发送还在进行中，下一轮继续
}

static int server_loop(void *arg)
{
    (void)arg;

    // 第一次调用时初始化 listenfd
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

        if (inet_pton(AF_INET, g_bind_ip, &addr.sin_addr) != 1) {
            perror("inet_pton g_bind_ip");
            return -1;
        }
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
        std::fprintf(stdout, "msg size: %zu\n", sizeof(Msg));
    }

    // 1) 单轮 accept 尽可能多的新连接
    for (;;) {
        int cfd = ff_accept(g_listenfd, nullptr, nullptr);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EINTR || errno == EPERM) {
                // 当前没有更多 pending 的连接
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

        // 初始化新客户端状态
        ClientState& state = g_client_states[g_client_count];
        state.fd = cfd;
        state.recv_bytes = 0;
        state.send_bytes = 0;
        state.has_full_msg = false;
        
        g_client_count++;
        // printf("new client fd=%d, total=%d\n", cfd, g_client_count);
    }

    // 2) 单轮遍历所有已连接的 client
    int idx = 0;
    while (idx < g_client_count) {
        int current_client_count = g_client_count;  // 保存当前值
        
        process_one_client(idx);
        
        // 如果客户端被移除，g_client_count会减少
        // 如果当前客户端还在，继续处理下一个
        if (g_client_count == current_client_count) {
            // 客户端没被移除
            idx++;
        }
        // 如果客户端被移除了，idx保持不变（因为下一个客户端已经移动到当前idx位置）
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
