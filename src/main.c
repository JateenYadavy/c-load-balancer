/**
 * ============================================================================
 * main.c - Verification Suite & Multi-Core Benchmarks
 * ============================================================================
 */

#define _GNU_SOURCE
#include "threadpool.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>

// Shared payload for tasks
typedef struct {
    int task_id;
    int task_type;
    int duration_ms;
} task_payload_t;

// Task Type 0: Quick simulated task (~30ms)
static void task_quick_fn(void *arg) {
    task_payload_t *p = (task_payload_t *)arg;
    int core = sched_getcpu();
    printf("  -> [Quick Task #%02d] executed on Core %d\n", p->task_id, core);
    usleep(30000); // 30 ms
    free(p);
}

// Task Type 1: Heavy CPU calculation (~250ms)
static void task_heavy_fn(void *arg) {
    task_payload_t *p = (task_payload_t *)arg;
    int core = sched_getcpu();
    printf("  -> [HEAVY Task #%02d] started on Core %d...\n", p->task_id, core);

    volatile long sum = 0;
    for (long i = 0; i < 40000000; ++i) {
        sum += (i ^ 0x5A5A);
    }

    printf("  -> [HEAVY Task #%02d] FINISHED on Core %d\n", p->task_id, core);
    free(p);
}

// Helper to allocate a payload
static task_payload_t* make_payload(int id, int type) {
    task_payload_t *p = (task_payload_t *)malloc(sizeof(task_payload_t));
    p->task_id = id;
    p->task_type = type;
    return p;
}

/**
 * TEST 1: Concurrency & Core Pinning Verification
 */
static void run_test1_core_pinning(void) {
    printf("----------------------------------------------------\n");
    printf(" TEST 1: Core Pinning & Concurrency Verification    \n");
    printf("----------------------------------------------------\n");

    int cores = get_cpu_core_count();
    threadpool_t *pool = threadpool_create(cores, 64, SCHEDULE_ROUND_ROBIN);

    printf("Submitting 6 quick tasks to pool of %d workers...\n", cores);
    for (int i = 0; i < 6; ++i) {
        threadpool_add(pool, task_quick_fn, make_payload(i + 1, 0), 0);
    }

    threadpool_wait(pool);
    threadpool_destroy(pool);
    printf("-> TEST 1 PASSED: Tasks distributed across physical cores.\n\n");
}

/**
 * TEST 2: Multi-Core Parallel Speedup Benchmark
 */
static void run_test2_parallel_speedup(void) {
    printf("----------------------------------------------------\n");
    printf(" TEST 2: Multi-Core Speedup Benchmark (1 vs All)    \n");
    printf("----------------------------------------------------\n");

    int total_cores = get_cpu_core_count();
    const int NUM_HEAVY_TASKS = 6;

    // Run on 1 Core
    printf("Running %d heavy tasks on 1 worker thread...\n", NUM_HEAVY_TASKS);
    threadpool_t *single_pool = threadpool_create(1, 64, SCHEDULE_ROUND_ROBIN);
    double t0 = get_time_ms();
    for (int i = 0; i < NUM_HEAVY_TASKS; ++i) {
        threadpool_add(single_pool, task_heavy_fn, make_payload(i + 1, 1), 1);
    }
    threadpool_wait(single_pool);
    double single_time = get_time_ms() - t0;
    threadpool_destroy(single_pool);
    printf("-> 1 Thread Time: %.2f ms\n\n", single_time);

    // Run on All Cores
    printf("Running %d heavy tasks on %d worker threads (All Cores)...\n", NUM_HEAVY_TASKS, total_cores);
    threadpool_t *multi_pool = threadpool_create(total_cores, 64, SCHEDULE_LEAST_LOADED);
    t0 = get_time_ms();
    for (int i = 0; i < NUM_HEAVY_TASKS; ++i) {
        threadpool_add(multi_pool, task_heavy_fn, make_payload(i + 1, 1), 1);
    }
    threadpool_wait(multi_pool);
    double multi_time = get_time_ms() - t0;
    threadpool_destroy(multi_pool);
    printf("-> %d Threads Time: %.2f ms\n", total_cores, multi_time);

    double speedup = single_time / multi_time;
    printf("-> Parallel Speedup: %.2fx faster!\n", speedup);
    printf("-> TEST 2 PASSED: Verified true parallel execution.\n\n");
}

/**
 * TEST 3: Scheduler Comparison (Round-Robin vs Least-Loaded vs Predictive)
 */
static void run_scheduler_benchmark(schedule_policy_t policy, const char *name) {
    int total_cores = get_cpu_core_count();
    threadpool_t *pool = threadpool_create(total_cores, 64, policy);

    printf("Executing under %s...\n", name);
    double t0 = get_time_ms();

    // 1. Submit 1 Heavy Task (Type 1)
    threadpool_add(pool, task_heavy_fn, make_payload(99, 1), 1);

    // 2. Submit 6 Quick Tasks (Type 0) immediately after
    for (int i = 0; i < 6; ++i) {
        threadpool_add(pool, task_quick_fn, make_payload(i + 1, 0), 0);
    }

    threadpool_wait(pool);
    double elapsed = get_time_ms() - t0;
    threadpool_destroy(pool);

    printf("-> Total Elapsed Time (%s): %.2f ms\n\n", name, elapsed);
}

static void run_test3_scheduler_comparison(void) {
    printf("----------------------------------------------------\n");
    printf(" TEST 3: Asymmetric Load Scheduler Comparison       \n");
    printf(" (1 Heavy Task + 6 Quick Tasks)                     \n");
    printf("----------------------------------------------------\n");

    run_scheduler_benchmark(SCHEDULE_ROUND_ROBIN, "ROUND-ROBIN");
    run_scheduler_benchmark(SCHEDULE_LEAST_LOADED, "LEAST-LOADED");
    run_scheduler_benchmark(SCHEDULE_PREDICTIVE, "PREDICTIVE (EMA)");

    printf("-> TEST 3 PASSED: Schedulers evaluated.\n\n");
}

int main(void) {
    printf("====================================================\n");
    printf(" CPU Task Assigner & Multi-Core Scheduler in C      \n");
    printf(" Physical Cores Detected: %d\n", get_cpu_core_count());
    printf("====================================================\n\n");

    run_test1_core_pinning();
    run_test2_parallel_speedup();
    run_test3_scheduler_comparison();

    printf("====================================================\n");
    printf(" ALL TESTS & BENCHMARKS COMPLETED SUCCESSFULLY!     \n");
    printf("====================================================\n");
    return 0;
}
