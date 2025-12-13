// common.h
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <stdio.h>

const int MSG_SIZE = 64;

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

#pragma pack(push, 1)
struct Msg {
    uint64_t send_ns;        // client 发出的时间戳
    char payload[MSG_SIZE - sizeof(uint64_t)];       // 随便占点空间
};
#pragma pack(pop)

struct LatencyStats {
    uint64_t count;
    uint64_t sum_ns;
    uint64_t min_ns;
    uint64_t max_ns;
};

static inline void stats_init(struct LatencyStats *s) {
    s->count = 0;
    s->sum_ns = 0;
    s->min_ns = (uint64_t)-1;
    s->max_ns = 0;
}

static inline void stats_add(struct LatencyStats *s, uint64_t ns) {
    s->count++;
    s->sum_ns += ns;
    if (ns < s->min_ns) s->min_ns = ns;
    if (ns > s->max_ns) s->max_ns = ns;
}

static inline void stats_print(const char *tag, const struct LatencyStats *s) {
    if (s->count == 0) {
        printf("%s: no samples\n", tag);
        return;
    }
    double avg_us = (double)s->sum_ns / s->count / 1000.0;
    double min_us = (double)s->min_ns / 1000.0;
    double max_us = (double)s->max_ns / 1000.0;
    printf("%s: count=%" PRIu64 ", avg=%.2f us, min=%.2f us, max=%.2f us\n",
           tag, s->count, avg_us, min_us, max_us);
}

#endif // COMMON_H

