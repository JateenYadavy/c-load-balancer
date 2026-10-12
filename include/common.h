#ifndef COMMON_H
#define COMMON_H

#ifdef __linux__
    #define _GNU_SOURCE
    #include <sched.h>
#endif

#include <time.h>
#include <unistd.h>
#include <pthread.h>

// 64-byte alignment macro to prevent false sharing across CPU cores
#define CACHE_LINE_SIZE 64
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))

/**
 * Pins the calling (or specified) thread to a dedicated physical CPU core.
 * @param thread Thread handle to pin.
 * @param core_id Physical core ID (0 to num_cores - 1).
 */
static inline void pin_thread_to_core(pthread_t thread, int core_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
#elif defined(__APPLE__)
    // macOS manages core placement automatically across Performance/Efficiency cores
    (void)thread;
    (void)core_id;
#else
    (void)thread;
    (void)core_id;
#endif
}

/**
 * Returns the current high-resolution monotonic time in milliseconds.
 */
static inline double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

/**
 * Returns the number of active, online physical CPU cores on this machine.
 */
static inline int get_cpu_core_count(void) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    return (cores > 0) ? (int)cores : 1;
}

#endif // COMMON_H
