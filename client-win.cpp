// client_win.cpp
// Windows + Visual Studio 上用的 echo RTT 测试 client
// 对应 Linux 端的 common.h:
//
// struct Msg {
//     uint64_t send_ns;
//     char     payload[256];
// };
//
// 用法: client_win.exe <server_ip> <port> <count>

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>

#pragma comment(lib, "ws2_32.lib")

// ===================== 协议结构体 =====================
// 必须和 Linux 端 common.h 完全一致

const int MSG_SIZE = 8196;

#pragma pack(push, 1)
struct Msg {
    uint64_t send_ns;       // client 发出的时间戳 (ns, 任意单调时钟)
    char     payload[MSG_SIZE - sizeof(uint64_t)];  // 只是占空间，不参与计算
                            // todo, change size
};
#pragma pack(pop)

//static_assert(sizeof(Msg) == 8 + 256, "Msg layout mismatch with Linux common.h");

// ===================== 时间函数 =====================

// 用 steady_clock 当成 Windows 版的 CLOCK_MONOTONIC_RAW
static uint64_t now_ns()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(
        steady_clock::now().time_since_epoch()
        ).count();
}

// ===================== 发送 / 接收辅助函数 =====================

static bool send_all(SOCKET s, const char* buf, int len)
{
    int sent = 0;
    while (sent < len) {
        int n = ::send(s, buf + sent, len - sent, 0);
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            fprintf(stderr, "send failed, WSA error = %d\n", err);
            return false;
        }
        if (n == 0) {
            fprintf(stderr, "send returned 0 (connection closed).\n");
            return false;
        }
        sent += n;
    }
    return true;
}

static bool recv_all(SOCKET s, char* buf, int len)
{
    int recvd = 0;
    while (recvd < len) {
        int n = ::recv(s, buf + recvd, len - recvd, 0);
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            fprintf(stderr, "recv failed, WSA error = %d\n", err);
            return false;
        }
        if (n == 0) {
            fprintf(stderr, "recv returned 0 (connection closed).\n");
            return false;
        }
        recvd += n;
        // check if need to call recv more than 1 time
    }
    return true;
}

void print_statistics(std::vector<uint64_t>& rtts) {
    int count = rtts.size();
    if (count <= 0) {
        printf("No data to print\n");
        return;
    }

    // 排序计算百分位
    sort(rtts.begin(), rtts.end());

    uint64_t sum = std::accumulate(rtts.begin(), rtts.end(), 0ull);
    //printf("sum %llu\n", sum);
    uint64_t min = rtts[0];
    uint64_t max = rtts[rtts.size()-1];

    double avg = (long double)sum / count;
    uint64_t median = rtts[count / 2];
    uint64_t p90 = rtts[(int)(count * 0.9)];
    uint64_t p99 = rtts[(int)(count * 0.99)];
    uint64_t p999 = rtts[(int)(count * 0.999)];

    // 计算标准差
    long double variance = 0;
    for (int i = 0; i < count; i++) {
        double diff = rtts[i] - avg;
        variance += diff * diff;
    }
    double stddev = sqrt(variance / count);

    printf("\n=== 延迟统计 ===\n");
    printf("测试次数: %d\n", count);
    printf("平均延迟: %.2f ns (%.3f us)\n", avg, avg / 1000.0);
    printf("中位数:   %d ns (%.3f us)\n", median, median / 1000.0);
    printf("最小值:   %d ns (%.3f us)\n", min, min / 1000.0);
    printf("最大值:   %d ns (%.3f us)\n", max, max / 1000.0);
    printf("标准差:   %.2f ns (%.3f us)\n", stddev, stddev / 1000.0);
    printf("P50:      %d ns (%.3f us)\n", median, median / 1000.0);
    printf("P90:      %d ns (%.3f us)\n", p90, p90 / 1000.0);
    printf("P99:      %d ns (%.3f us)\n", p99, p99 / 1000.0);
    printf("P99.9:    %d ns (%.3f us)\n", p999, p999 / 1000.0);
    printf("吞吐量:   %.2f requests/sec\n", 1e9 / avg * 1);
}

// ===================== 主程序 =====================

int main(int argc, char* argv[])
{
    fprintf(stdout, "Msg Size: %d\n", MSG_SIZE);

    if (argc < 4) {
        fprintf(stderr, "Usage: %s <server_ip> <port> <count>\n", argv[0]);
        return 1;
    }

    const char* server_ip = argv[1];
    int port = atoi(argv[2]);
    int count = atoi(argv[3]);

    if (count <= 0) {
        fprintf(stderr, "count must be > 0\n");
        return 1;
    }

    // 初始化 Winsock
    WSADATA wsaData;
    int r = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (r != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", r);
        return 1;
    }

    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // 关闭 Nagle，降低 RTT 抖动
    BOOL flag = TRUE;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
        (const char*)&flag, sizeof(flag));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);

    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "inet_pton failed for ip: %s\n", server_ip);
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("Connecting to %s:%d ...\n", server_ip, port);
    if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "connect() failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    printf("Connected.\n");

    std::vector<uint64_t> rtts;
    rtts.reserve(count);

    Msg msg{};
    Msg echo{};

    // warm up
    for (int i = 0; i < 500; ++i) {
        msg.send_ns = now_ns();

        // payload 随便填点内容（不用也可以）
        // 这里就简单写死第一个字节，防止被奇怪的优化干掉
        msg.payload[0] = (char)(i & 0xFF);

        if (!send_all(sock, (const char*)&msg, (int)sizeof(msg))) {
            fprintf(stderr, "send_all failed at i=%d\n", i);
            break;
        }

        if (!recv_all(sock, (char*)&echo, (int)sizeof(echo))) {
            fprintf(stderr, "recv_all failed at i=%d\n", i);
            break;
        }

        uint64_t now = now_ns();
        uint64_t rtt = now - msg.send_ns; // ns
    }

    // test
    for (int i = 0; i < count; ++i) {
        msg.send_ns = now_ns();

        // payload 随便填点内容（不用也可以）
        // 这里就简单写死第一个字节，防止被奇怪的优化干掉
        msg.payload[0] = (char)(i & 0xFF);

        if (!send_all(sock, (const char*)&msg, (int)sizeof(msg))) {
            fprintf(stderr, "send_all failed at i=%d\n", i);
            break;
        }

        if (!recv_all(sock, (char*)&echo, (int)sizeof(echo))) {
            fprintf(stderr, "recv_all failed at i=%d\n", i);
            break;
        }

        uint64_t now = now_ns();
        uint64_t rtt = now - msg.send_ns; // ns

        rtts.push_back(rtt);

        //if (i < 5) {
        //    printf("i=%d, RTT = %.3f us\n", i, rtt / 1000.0);
        //}
    }

    closesocket(sock);
    WSACleanup();

    if (rtts.empty()) {
        fprintf(stderr, "No RTT data collected.\n");
        return 1;
    }

    print_statistics(rtts);

    return 0;
}

