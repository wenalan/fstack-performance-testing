// Windows echo RTT client aligned with client.cpp logic.

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <cerrno>
#include <direct.h>
#include <inttypes.h>
#include <numeric>
#include <string>
#include <vector>
#include <utility>
#include <sys/stat.h>

#pragma comment(lib, "ws2_32.lib")

static constexpr const char* kOutputDir = "output";

struct LatencySummary {
    uint32_t payload_size = 0;
    int sample_count = 0;
    double avg_ns = 0.0;
    uint64_t min_ns = 0;
    uint64_t max_ns = 0;
    uint64_t p50_ns = 0;
    uint64_t p90_ns = 0;
    uint64_t p99_ns = 0;
    uint64_t p999_ns = 0;
    double variance_ns2 = 0.0;
    double throughput_rps = 0.0;
};

#pragma pack(push, 1)
struct Msg {
    uint32_t payload_size;
};
#pragma pack(pop)

static uint64_t now_ns()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch())
        .count();
}

static bool send_all(SOCKET s, const void* buffer, size_t len)
{
    const auto* data = static_cast<const char*>(buffer);
    size_t sent = 0;
    while (sent < len) {
        size_t remaining = len - sent;
        int chunk = static_cast<int>(remaining > static_cast<size_t>(INT_MAX)
                                         ? INT_MAX
                                         : remaining);
        int n = ::send(s, data + sent, chunk, 0);
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEINTR) {
                continue;
            }
            if (err == WSAEWOULDBLOCK) {
                fprintf(stderr, "send would block\n");
                continue;
            }
            fprintf(stderr, "send failed, WSA error = %d\n", err);
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

static bool recv_all(SOCKET s, void* buffer, size_t len)
{
    auto* data = static_cast<char*>(buffer);
    size_t recvd = 0;
    while (recvd < len) {
        size_t remaining = len - recvd;
        int chunk = static_cast<int>(remaining > static_cast<size_t>(INT_MAX)
                                         ? INT_MAX
                                         : remaining);
        int n = ::recv(s, data + recvd, chunk, 0);
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEINTR) {
                continue;
            }
            if (err == WSAEWOULDBLOCK) {
                fprintf(stderr, "recv would block\n");
                continue;
            }
            fprintf(stderr, "recv failed, WSA error = %d\n", err);
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

static bool recv_message(SOCKET s, std::vector<char>& buffer)
{
    buffer.resize(sizeof(Msg));
    if (!recv_all(s, buffer.data(), sizeof(Msg))) {
        return false;
    }

    const auto* header = reinterpret_cast<const Msg*>(buffer.data());
    if (header->payload_size < sizeof(Msg)) {
        fprintf(stderr,
                "server payload_size=%" PRIu32 " is smaller than header size %zu\n",
                header->payload_size,
                sizeof(Msg));
        return false;
    }

    const size_t payload_bytes = header->payload_size - sizeof(Msg);
    buffer.resize(header->payload_size);
    if (payload_bytes > 0 &&
        !recv_all(s, buffer.data() + sizeof(Msg), payload_bytes)) {
        return false;
    }

    return true;
}

static bool compute_statistics(const std::vector<uint64_t>& rtts,
                               uint32_t payload_size,
                               LatencySummary* summary)
{
    if (rtts.empty()) {
        return false;
    }

    std::vector<uint64_t> sorted = rtts;
    std::sort(sorted.begin(), sorted.end());

    const int count = static_cast<int>(sorted.size());
    const uint64_t sum =
        std::accumulate(sorted.begin(), sorted.end(), static_cast<uint64_t>(0));
    const uint64_t min = sorted.front();
    const uint64_t max = sorted.back();

    const double avg = static_cast<long double>(sum) / count;
    const auto percentile = [&](double ratio) {
        if (sorted.empty()) {
            return uint64_t{0};
        }
        double position = ratio * (sorted.size() - 1);
        size_t idx = static_cast<size_t>(std::llround(position));
        if (idx >= sorted.size()) {
            idx = sorted.size() - 1;
        }
        return sorted[idx];
    };
    const uint64_t p50 = percentile(0.5);
    const uint64_t p90 = percentile(0.9);
    const uint64_t p99 = percentile(0.99);
    const uint64_t p999 = percentile(0.999);

    long double variance_acc = 0;
    for (uint64_t value : sorted) {
        const long double diff = static_cast<long double>(value) - avg;
        variance_acc += diff * diff;
    }
    const double variance = static_cast<double>(variance_acc / count);
    const double throughput = (avg > 0.0) ? (1e9 / avg) : 0.0;

    summary->payload_size = payload_size;
    summary->sample_count = count;
    summary->avg_ns = avg;
    summary->min_ns = min;
    summary->max_ns = max;
    summary->p50_ns = p50;
    summary->p90_ns = p90;
    summary->p99_ns = p99;
    summary->p999_ns = p999;
    summary->variance_ns2 = variance;
    summary->throughput_rps = throughput;
    return true;
}

static void print_statistics(const LatencySummary& s)
{
    printf("\n=== Latency Statistics ===\n");
    printf("Payload size: %" PRIu32 " bytes\n", s.payload_size);
    printf("Samples: %d\n", s.sample_count);
    printf("Average latency: %.2f ns (%.3f us)\n", s.avg_ns, s.avg_ns / 1000.0);
    printf("Median (P50): %" PRIu64 " ns (%.3f us)\n", s.p50_ns, s.p50_ns / 1000.0);
    printf("P90: %" PRIu64 " ns (%.3f us)\n", s.p90_ns, s.p90_ns / 1000.0);
    printf("P99: %" PRIu64 " ns (%.3f us)\n", s.p99_ns, s.p99_ns / 1000.0);
    printf("P99.9: %" PRIu64 " ns (%.3f us)\n", s.p999_ns, s.p999_ns / 1000.0);
    printf("Minimum: %" PRIu64 " ns (%.3f us)\n", s.min_ns, s.min_ns / 1000.0);
    printf("Maximum: %" PRIu64 " ns (%.3f us)\n", s.max_ns, s.max_ns / 1000.0);
    printf("Variance: %.2f ns^2\n", s.variance_ns2);
    printf("Throughput: %.2f requests/sec\n", s.throughput_rps);
}

static bool validate_payload_args(uint32_t payload_size, int msg_count)
{
    if (msg_count <= 0) {
        fprintf(stderr, "msg_count must be > 0\n");
        return false;
    }
    if (payload_size < sizeof(Msg)) {
        fprintf(stderr,
                "payload_size must be >= %zu bytes\n",
                sizeof(Msg));
        return false;
    }
    return true;
}

static SOCKET connect_tcp(const char* server_ip, int port)
{
    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
        return INVALID_SOCKET;
    }

    BOOL flag = TRUE;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&flag), sizeof(flag));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "inet_pton failed for ip: %s\n", server_ip);
        closesocket(sock);
        return INVALID_SOCKET;
    }

    if (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) ==
        SOCKET_ERROR) {
        fprintf(stderr, "connect() failed: %d\n", WSAGetLastError());
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

static bool run_payload_test_on_socket(SOCKET sock,
                                       const char* server_ip,
                                       int port,
                                       uint32_t payload_size,
                                       int msg_count,
                                       LatencySummary* summary,
                                       std::vector<uint64_t>* samples,
                                       bool print_result = true,
                                       bool skip_validation = false)
{
    if (!skip_validation && !validate_payload_args(payload_size, msg_count)) {
        return false;
    }

    if (print_result) {
        printf("\nConnected to %s:%d with payload_size=%" PRIu32
               ", sending %d messages...\n",
               server_ip,
               port,
               payload_size,
               msg_count);
    }

    std::vector<char> send_buffer(payload_size);
    auto* header = reinterpret_cast<Msg*>(send_buffer.data());
    header->payload_size = payload_size;
    const size_t payload_bytes = payload_size - sizeof(Msg);
    char* payload_start = send_buffer.data() + sizeof(Msg);
    std::fill(payload_start, payload_start + payload_bytes, 0x42);

    std::vector<char> recv_buffer;
    std::vector<uint64_t> rtts;
    rtts.reserve(msg_count);

    for (int i = 0; i < msg_count; ++i) {
        const uint64_t send_ts = now_ns();

        if (!send_all(sock, send_buffer.data(), send_buffer.size())) {
            fprintf(stderr, "send_all failed at i=%d\n", i);
            return false;
        }

        if (!recv_message(sock, recv_buffer)) {
            fprintf(stderr, "recv_message failed at i=%d\n", i);
            return false;
        }

        uint64_t now = now_ns();
        uint64_t rtt_ns = now - send_ts;
        rtts.push_back(rtt_ns);
    }

    if (!compute_statistics(rtts, payload_size, summary)) {
        fprintf(stderr, "No RTT data collected for payload_size=%" PRIu32 "\n",
                payload_size);
        return false;
    }

    if (samples) {
        *samples = std::move(rtts);
    }

    if (print_result) {
        print_statistics(*summary);
    }
    return true;
}

static std::string make_csv_basename(const char* basename)
{
    std::string name = (basename && basename[0] != '\0')
                           ? std::string(basename)
                           : std::string("output");
    const std::string suffix = ".csv";
    if (name.length() >= suffix.length() &&
        name.compare(name.length() - suffix.length(), suffix.length(), suffix) == 0) {
        name.erase(name.length() - suffix.length());
    }
    return name;
}

static bool ensure_directory_exists(const std::string& path)
{
    struct _stat info {};
    if (_stat(path.c_str(), &info) == 0) {
        return (info.st_mode & _S_IFDIR) != 0;
    }
    if (errno != ENOENT) {
        char err_buf[256];
        strerror_s(err_buf, sizeof(err_buf), errno);
        fprintf(stderr, "Failed to access %s: %s\n", path.c_str(), err_buf);
        return false;
    }
    if (_mkdir(path.c_str()) == 0 || errno == EEXIST) {
        return true;
    }
    char err_buf[256];
    strerror_s(err_buf, sizeof(err_buf), errno);
    fprintf(stderr, "Failed to create directory %s: %s\n", path.c_str(), err_buf);
    return false;
}

int main(int argc, char* argv[])
{
    if (argc < 5) {
        fprintf(stderr,
                "Usage: %s <server_ip> <port> <msg_count> <payload_size|-1> "
                "[output_basename]\n",
                argv[0]);
        return 1;
    }

    const char* server_ip = argv[1];
    int port = atoi(argv[2]);

    char* endptr = nullptr;
    long msg_count_long = strtol(argv[3], &endptr, 10);
    if (*endptr != '\0' || msg_count_long <= 0 || msg_count_long > INT_MAX) {
        fprintf(stderr, "msg_count must be a positive integer\n");
        return 1;
    }
    int msg_count = static_cast<int>(msg_count_long);

    char* payload_end = nullptr;
    long payload_arg = strtol(argv[4], &payload_end, 10);
    if (*payload_end != '\0') {
        fprintf(stderr, "payload_size must be an integer or -1\n");
        return 1;
    }
    const char* output_basename = (argc >= 6) ? argv[5] : nullptr;

    bool sweep_payloads = (payload_arg == -1);
    if (sweep_payloads && (!output_basename || output_basename[0] == '\0')) {
        fprintf(stderr, "output_basename is required when payload_size is -1.\n");
        return 1;
    }

    std::vector<uint32_t> payload_sizes;
    if (sweep_payloads) {
        const uint32_t presets[] = {512, 1024, 2048, 4096, 8192, 64, 128, 256};
        payload_sizes.assign(presets,
                             presets + (sizeof(presets) / sizeof(presets[0])));
    } else {
        if (payload_arg <= 0) {
            fprintf(stderr, "payload_size must be positive or -1\n");
            return 1;
        }
        payload_sizes.push_back(static_cast<uint32_t>(payload_arg));
    }

    std::string output_base;
    std::string output_dir;
    std::ofstream summary_file;
    std::string summary_path;

    output_dir = kOutputDir;
    if (!ensure_directory_exists(output_dir)) {
        fprintf(stderr, "Failed to create output directory: %s\n", output_dir.c_str());
        return 1;
    }
    output_base = make_csv_basename(output_basename);
    summary_path = output_dir + "/" + output_base + "_sum.csv";
    summary_file.open(summary_path);
    if (!summary_file.is_open()) {
        fprintf(stderr, "Failed to open %s for writing\n", summary_path.c_str());
        return 1;
    }
    summary_file << "payload_size,avg_latency_ns,min_latency_ns,p50_ns,"
                    "p90_ns,p99_ns,p99.9_ns,max_latency_ns,throughput_rps\n";

    WSADATA wsaData;
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_result != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", wsa_result);
        return 1;
    }

    SOCKET shared_sock = connect_tcp(server_ip, port);
    if (shared_sock == INVALID_SOCKET) {
        if (summary_file.is_open()) {
            summary_file.close();
        }
        WSACleanup();
        return 1;
    }

    bool overall_success = true;
    for (size_t idx = 0; idx < payload_sizes.size(); ++idx) {
        uint32_t payload_size = payload_sizes[idx];

        if (idx == 0) {
            LatencySummary warmup_summary{};
            if (!run_payload_test_on_socket(shared_sock,
                                            server_ip,
                                            port,
                                            payload_size,
                                            msg_count,
                                            &warmup_summary,
                                            nullptr,
                                            false)) {
                overall_success = false;
            }
        }

        LatencySummary summary{};
        std::vector<uint64_t> samples;
        bool ok = run_payload_test_on_socket(shared_sock,
                                            server_ip,
                                            port,
                                            payload_size,
                                            msg_count,
                                            &summary,
                                            sweep_payloads ? &samples : nullptr);
        if (!ok) {
            overall_success = false;
            continue;
        }

        if (summary_file.is_open()) {
            summary_file << summary.payload_size << ',' << summary.avg_ns << ','
                         << summary.min_ns << ',' << summary.p50_ns << ','
                         << summary.p90_ns << ',' << summary.p99_ns << ','
                         << summary.p999_ns << ',' << summary.max_ns << ','
                         << summary.throughput_rps << '\n';

            const std::string detail_path =
                output_dir + "/" + output_base + "_" + std::to_string(payload_size) + ".csv";
            std::ofstream detail_file(detail_path);
            if (!detail_file.is_open()) {
                fprintf(stderr, "Failed to open %s for writing\n", detail_path.c_str());
                overall_success = false;
                continue;
            }
            detail_file << "latency_ns\n";
            for (uint64_t value : samples) {
                detail_file << value << '\n';
            }
        }
    }

    if (shared_sock != INVALID_SOCKET) {
        closesocket(shared_sock);
    }

    if (summary_file.is_open()) {
        summary_file.close();
        printf("\nAggregated results written to %s\n", summary_path.c_str());
    }

    WSACleanup();

    return overall_success ? 0 : 1;
}
