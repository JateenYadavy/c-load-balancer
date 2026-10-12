#ifndef THREADPOOL_INTERNAL_H
#define THREADPOOL_INTERNAL_H

#include "threadpool.h"
#include "common.h"
#include <stdbool.h>

#define MAX_TASK_TYPES 32
#define EMA_ALPHA 0.2 // 20% weight to newest execution, 80% weight to history

/**
 * Per-Worker Structure
 * Aligned to 64 bytes to eliminate false sharing across CPU cores.
 */
typedef struct {
    pthread_t thread;
    int worker_id;
    int core_id;

    // Private ring-buffer task queue
    task_t *queue;
    int queue_head;
    int queue_tail;
    int queue_count;
    int queue_capacity;

    // Synchronization
    pthread_mutex_t lock;
    pthread_cond_t has_work;

    // Status tracking for schedulers
    int is_busy;
    int current_task_type;
    bool is_shutdown;

    // Reference to parent thread pool
    threadpool_t *pool;
} CACHE_ALIGNED worker_t;

/**
 * Internal Scheduler State (EMA table & Round-Robin cursor)
 */
typedef struct {
    double predicted_duration_ms[MAX_TASK_TYPES];
    int next_round_robin_worker;
    pthread_mutex_t stats_lock;
} scheduler_state_t;

/**
 * Main Thread Pool Structure
 */
struct threadpool {
    worker_t *workers;
    int num_threads;
    int queue_capacity;
    schedule_policy_t policy;

    // Global tracking & wait condition
    pthread_mutex_t pool_lock;
    pthread_cond_t all_idle;
    int active_tasks_total;
    bool is_shutdown;

    // Internal scheduler state
    scheduler_state_t *scheduler_state;
};

#endif // THREADPOOL_INTERNAL_H
