// server_kernel.cpp
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"

constexpr int LISTEN_PORT = 8081;
constexpr int BACKLOG = 1024;

static bool recv_full_msg(int fd, Msg &msg) {
    size_t received = 0;
    auto *buffer = reinterpret_cast<char*>(&msg);

    printf("size of msg is %d\n", (int)sizeof(Msg));

    while (received < sizeof(Msg)) {
        ssize_t n = recv(fd, buffer + received,
                         sizeof(Msg) - received, 0);
    printf("size of msg is %d\n", (int)n);
        if (n == 0) {
            // peer closed
            return false;
        }
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("recv");
            return false;
        }

        received += static_cast<size_t>(n);
    }

    return true;
}

static bool send_full_msg(int fd, const Msg &msg) {
    size_t sent = 0;
    auto *buffer = reinterpret_cast<const char*>(&msg);

    while (sent < sizeof(Msg)) {
        ssize_t n = send(fd, buffer + sent,
                         sizeof(Msg) - sent, 0);
        if (n == 0) {
            // peer closed while we were sending
            return false;
        }
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("send");
            return false;
        }

        sent += static_cast<size_t>(n);
    }

    return true;
}

static void handle_conn(int fd) {
    Msg msg{};
    for (;;) {
        if (!recv_full_msg(fd, msg))
            break;
        if (!send_full_msg(fd, msg))
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
    printf("Msg size is %d\n", MSG_SIZE);

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

        // 简化：单连接处理
        handle_conn(conn_fd);
    }

    close(listen_fd);
    return 0;
}
