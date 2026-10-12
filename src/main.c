/**
 * ============================================================================
 * main.c - Verification Suite & Multi-Core Benchmarks
 * ============================================================================
 * 
 * WHAT WE WANT IN THIS FILE:
 * 1. Test 1: Concurrency & Core Pinning Verification:
 *    - Dispatches a batch of tasks that log:
 *      [Worker ID] running on [CPU Core ID] for [Task ID]
 *    - Confirms visually in the terminal that workers are pinned to distinct
 *      cores and executing in parallel.
 * 
 * 2. Test 2: Multi-Core Speedup Verification:
 *    - Executes CPU-intensive math/prime calculation tasks.
 *    - Compares execution time: 1 Worker vs. All CPU Cores.
 *    - Verifies linear speedup (e.g. ~3x faster on 3 cores).
 * 
 * 3. Test 3: Side-by-Side Scheduler Comparison:
 *    - Submits an asymmetric workload:
 *      * 1 Heavy Task (takes ~2 seconds).
 *      * 6 Quick Tasks (take ~50 milliseconds each).
 *    - Tests under:
 *      a. SCHEDULE_ROUND_ROBIN: Shows quick tasks getting trapped behind the heavy task
 *         (Head-of-Line Blocking / Convoy Effect).
 *      b. SCHEDULE_LEAST_LOADED: Shows quick tasks dynamically bypassing the heavy task.
 *      c. SCHEDULE_PREDICTIVE: Shows runtime EMA prediction anticipating completion
 *         times and routing for optimal overall latency.
 * ============================================================================
 */

#include "threadpool.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/*
 * Sample Task Definitions
 */

// Task Type 0: Quick simulated task (~50ms)
void task_quick(void *arg) {
    (void)arg;
    usleep(50000); // 50 ms
}

// Task Type 1: Heavy CPU computation (math / prime loops)
void task_heavy(void *arg) {
    (void)arg;
    volatile long sum = 0;
    for (long i = 0; i < 50000000; ++i) {
        sum += (i ^ 0x5A5A);
    }
}

int main(int argc, char *argv[]) {
    printf("====================================================\n");
    printf(" CPU Task Assigner & Multi-Core Scheduler in C      \n");
    printf(" Detected System CPU Cores: %d\n", get_cpu_core_count());
    printf("====================================================\n\n");

    /*
     * TODO (Stage 3):
     * Run Test 1 (Core Pinning Verification)
     * Run Test 2 (Parallel Speedup Benchmark)
     * Run Test 3 (Round-Robin vs Least-Loaded vs Predictive)
     */

    return 0;
}
