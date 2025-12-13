// client.cpp
#include <algorithm>
#include <cmath>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <numeric>
#include <vector>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"

constexpr int DEFAULT_MSG_COUNT = 100000;

static bool send_all(int fd, const void* buffer, size_t len)
{
    const auto* data = static_cast<const char*>(buffer);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;  // 信号中断，重试
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr, "send would block\n");
                continue;
            }

            fprintf(stderr, "send failed: %s\n", strerror(errno));
            return false;
        }
        if (n == 0) {
            fprintf(stderr, "send returned 0 (connection closed).\n");
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

static bool recv_all(int fd, void* buffer, size_t len)
{
    auto* data = static_cast<char*>(buffer);
    size_t recvd = 0;
    while (recvd < len) {
        ssize_t n = recv(fd, data + recvd, len - recvd, 0);
        if (n < 0) {  // Linux 中 recv 返回 -1 表示错误
            if (errno == EINTR) {
                continue;  // 被信号中断，继续接收
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr, "recv would block\n");
                continue;
            }

            fprintf(stderr, "recv failed: %s (errno = %d)\n",
                    strerror(errno), errno);
            return false;
        }
        if (n == 0) {
            fprintf(stderr, "recv returned 0 (connection closed).\n");
            return false;
        }
        recvd += static_cast<size_t>(n);
    }
    return true;
}

void print_statistics(const std::vector<uint64_t>& rtts) {
    if (rtts.empty()) {
        printf("No data to print\n");
        return;
    }

    std::vector<uint64_t> sorted = rtts;
    std::sort(sorted.begin(), sorted.end());

    const int count = static_cast<int>(sorted.size());
    const uint64_t sum =
        std::accumulate(sorted.begin(), sorted.end(), static_cast<uint64_t>(0));
    const uint64_t min = sorted.front();
    const uint64_t max = sorted.back();

    const double avg = static_cast<long double>(sum) / count;
    const uint64_t median = sorted[count / 2];
    const auto percentile = [&](double ratio) {
        size_t idx = static_cast<size_t>(ratio * count);
        if (idx >= sorted.size()) {
            idx = sorted.size() - 1;
        }
        return sorted[idx];
    };
    const uint64_t p90 = percentile(0.9);
    const uint64_t p99 = percentile(0.99);
    const uint64_t p999 = percentile(0.999);

    long double variance = 0;
    for (uint64_t value : sorted) {
        const long double diff = static_cast<long double>(value) - avg;
        variance += diff * diff;
    }
    const double stddev = std::sqrt(variance / count);

    printf("\n=== 延迟统计 ===\n");
    printf("测试次数: %d\n", count);
    printf("平均延迟: %.2f ns (%.3f us)\n", avg, avg / 1000.0);
    printf("中位数:   %" PRIu64 " ns (%.3f us)\n", median, median / 1000.0);
    printf("最小值:   %" PRIu64 " ns (%.3f us)\n", min, min / 1000.0);
    printf("最大值:   %" PRIu64 " ns (%.3f us)\n", max, max / 1000.0);
    printf("标准差:   %.2f ns (%.3f us)\n", stddev, stddev / 1000.0);
    printf("P50:      %" PRIu64 " ns (%.3f us)\n", median, median / 1000.0);
    printf("P90:      %" PRIu64 " ns (%.3f us)\n", p90, p90 / 1000.0);
    printf("P99:      %" PRIu64 " ns (%.3f us)\n", p99, p99 / 1000.0);
    printf("P99.9:    %" PRIu64 " ns (%.3f us)\n", p999, p999 / 1000.0);
    printf("吞吐量:   %.2f requests/sec\n", 1e9 / avg);
}

int main(int argc, char *argv[]) {
    
    fprintf(stdout, "msg size: %d\n", MSG_SIZE);

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port> [msg_count]\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    int msg_count = (argc >= 4) ? atoi(argv[3]) : DEFAULT_MSG_COUNT;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        return 1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    printf("Connected to %s:%d, sending %d messages...\n",
           server_ip, port, msg_count);

    struct LatencyStats rtt_stats;
    stats_init(&rtt_stats);

    struct Msg msg;
    struct Msg echo;
    memset(msg.payload, 0x42, sizeof(msg.payload)); // payload 填点东西

    std::vector<uint64_t> rtts;
    rtts.reserve(msg_count);

    for (int i = 0; i < 100; ++i) {
        msg.send_ns = now_ns();

        if (!send_all(fd, &msg, sizeof(msg))) {
            fprintf(stderr, "send_all failed at i=%d\n", i);
            break;
        }

        if (!recv_all(fd, &echo, sizeof(echo))) {
            fprintf(stderr, "recv_all failed at i=%d\n", i);
            break;
        }

        uint64_t now = now_ns();
        uint64_t rtt_ns = now - msg.send_ns;
    }

    for (int i = 0; i < msg_count; ++i) {
        msg.send_ns = now_ns();

        if (!send_all(fd, &msg, sizeof(msg))) {
            fprintf(stderr, "send_all failed at i=%d\n", i);
            break;
        }

        if (!recv_all(fd, &echo, sizeof(echo))) {
            fprintf(stderr, "recv_all failed at i=%d\n", i);
            break;
        }

        uint64_t now = now_ns();
        uint64_t rtt_ns = now - msg.send_ns;
	    rtts.push_back(rtt_ns);
    }

out:
    print_statistics(rtts);
    close(fd);
    return 0;
}
