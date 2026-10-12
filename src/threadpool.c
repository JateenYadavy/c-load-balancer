/**
 * ============================================================================
 * threadpool.c - Core Engine, Worker Loops, and Synchronization
 * ============================================================================
 * 
 * WHAT WE WANT IN THIS FILE:
 * 1. Per-Worker Structure (worker_t) with Cache-Line Padding:
 *    - Each worker thread has its own mailbox/queue to eliminate lock contention.
 *    - Pinned to a dedicated physical CPU core via pin_thread_to_core().
 *    - Aligned to 64 bytes (CACHE_ALIGNED) so Core 0 and Core 1 never fight over
 *      the same hardware cache line (eliminating false sharing / bus thrashing).
 * 
 * 2. The Worker Loop Lifecycle (worker_loop):
 *    - Runs in an infinite loop on its pinned core:
 *      a. Lock the worker's queue mutex.
 *      b. If no work, sleep in pthread_cond_wait (0% CPU).
 *      c. If shutdown is triggered and queue is drained, break and exit.
 *      d. Pop task from the queue.
 *      e. UNLOCK THE MUTEX IMMEDIATELY (Execute with ZERO locks held to prevent deadlocks!).
 *      f. Run task.function(task.arg) while measuring elapsed time.
 *      g. Feed runtime duration to the Predictive Scheduler (EMA update).
 *      h. Loop back to wait for the next task.
 * 
 * 3. Thread Pool Master Structure (threadpool_t):
 *    - Manages the array of workers, active scheduling policy, and total task tracking.
 * 
 * 4. Graceful Shutdown (threadpool_destroy):
 *    - Sets shutdown = 1.
 *    - Broadcasts to wake all sleeping workers (pthread_cond_broadcast).
 *    - Joins all worker threads (pthread_join) to ensure all in-flight work completes.
 *    - Destroys all mutexes, condition variables, and frees heap memory.
 * ============================================================================
 */

#include "threadpool.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/**
 * Per-Worker Structure
 * Aligned to 64 bytes to prevent cache line false sharing across CPU cores.
 */
typedef struct {
    pthread_t thread;
    int worker_id;
    int core_id;

    // Dedicated task queue for this worker
    task_t *queue;
    int queue_head;
    int queue_tail;
    int queue_count;
    int queue_capacity;

    // Synchronization
    pthread_mutex_t lock;
    pthread_cond_t has_work;

    // Status tracking for Least-Loaded & Predictive schedulers
    int is_busy;
    int current_task_type;

    // Reference to parent pool
    threadpool_t *pool;
} CACHE_ALIGNED worker_t;

/**
 * Main Thread Pool Structure
 */
struct threadpool {
    worker_t *workers;
    int num_threads;
    int queue_capacity;
    schedule_policy_t policy;

    // Global state & synchronization
    pthread_mutex_t pool_lock;
    pthread_cond_t all_idle;
    int active_tasks_total;
    bool is_shutdown;
};

/*
 * TODO (Stage 3):
 * 1. worker_loop(void *arg) - The worker's execution loop.
 * 2. threadpool_create() - Spawns workers and pins each to a core.
 * 3. threadpool_add() - Submits task to chosen worker via scheduler.
 * 4. threadpool_wait() - Waits until all queued/running tasks finish.
 * 5. threadpool_destroy() - Graceful teardown (broadcast -> join -> free).
 */
