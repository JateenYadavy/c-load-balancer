/**
 * ============================================================================
 * threadpool.c - Core Multi-Threaded Engine Implementation
 * ============================================================================
 */

#include "threadpool.h"
#include "threadpool_internal.h"
#include "scheduler.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/**
 * Worker Thread Execution Loop
 */
static void* worker_loop(void *arg) {
    worker_t *w = (worker_t *)arg;

    // 1. Permanently pin this worker thread to its dedicated CPU core
    pin_thread_to_core(pthread_self(), w->core_id);

    while (1) {
        pthread_mutex_lock(&w->lock);

        // 2. Sleep until a task arrives or worker is shutting down
        while (w->queue_count == 0 && !w->is_shutdown) {
            pthread_cond_wait(&w->has_work, &w->lock);
        }

        // 3. Graceful shutdown check: exit only when queue is drained
        if (w->is_shutdown && w->queue_count == 0) {
            pthread_mutex_unlock(&w->lock);
            break; // Exit worker loop cleanly
        }

        // 4. Pop task from the worker's ring buffer
        task_t task = w->queue[w->queue_head];
        w->queue_head = (w->queue_head + 1) % w->queue_capacity;
        w->queue_count--;
        w->is_busy = 1;
        w->current_task_type = task.task_type;

        // 5. UNLOCK IMMEDIATELY BEFORE RUNNING THE TASK!
        // Running tasks with zero locks held prevents all deadlocks
        pthread_mutex_unlock(&w->lock);

        // 6. Execute the task and measure runtime for predictive scheduler
        double t0 = get_time_ms();
        if (task.function != NULL) {
            task.function(task.arg);
        }
        double duration_ms = get_time_ms() - t0;

        // 7. Update worker status
        pthread_mutex_lock(&w->lock);
        w->is_busy = 0;
        pthread_mutex_unlock(&w->lock);

        // 8. Record runtime duration in the predictive scheduler's EMA table
        scheduler_record_completion(w->pool, task.task_type, duration_ms);

        // 9. Decrement active task counter and notify threadpool_wait() if all tasks finished
        pthread_mutex_lock(&w->pool->pool_lock);
        w->pool->active_tasks_total--;
        if (w->pool->active_tasks_total == 0) {
            pthread_cond_broadcast(&w->pool->all_idle);
        }
        pthread_mutex_unlock(&w->pool->pool_lock);
    }

    return NULL;
}

/**
 * Creates and initializes the thread pool
 */
threadpool_t* threadpool_create(int num_threads, int queue_capacity, schedule_policy_t policy) {
    if (num_threads <= 0) {
        num_threads = get_cpu_core_count();
    }
    if (queue_capacity <= 0) {
        queue_capacity = 1024;
    }

    threadpool_t *pool = (threadpool_t *)malloc(sizeof(threadpool_t));
    if (!pool) return NULL;

    pool->num_threads = num_threads;
    pool->queue_capacity = queue_capacity;
    pool->policy = policy;
    pool->active_tasks_total = 0;
    pool->is_shutdown = false;
    pool->scheduler_state = NULL;

    pthread_mutex_init(&pool->pool_lock, NULL);
    pthread_cond_init(&pool->all_idle, NULL);

    pool->workers = (worker_t *)aligned_alloc(CACHE_LINE_SIZE, sizeof(worker_t) * num_threads);
    if (!pool->workers) {
        free(pool);
        return NULL;
    }

    int total_cores = get_cpu_core_count();

    // Initialize and spawn each worker thread
    for (int i = 0; i < num_threads; ++i) {
        worker_t *w = &pool->workers[i];
        w->worker_id = i;
        w->core_id = i % total_cores; // Distribute across available physical cores
        w->queue_head = 0;
        w->queue_tail = 0;
        w->queue_count = 0;
        w->queue_capacity = queue_capacity;
        w->is_busy = 0;
        w->current_task_type = 0;
        w->is_shutdown = false;
        w->pool = pool;

        w->queue = (task_t *)malloc(sizeof(task_t) * queue_capacity);
        pthread_mutex_init(&w->lock, NULL);
        pthread_cond_init(&w->has_work, NULL);

        pthread_create(&w->thread, NULL, worker_loop, w);
    }

    // Initialize scheduler state
    scheduler_init(pool);

    return pool;
}

/**
 * Submits a task to the pool
 */
int threadpool_add(threadpool_t *pool, task_fn function, void *arg, int task_type) {
    if (!pool || !function) return -1;

    task_t task;
    task.function = function;
    task.arg = arg;
    task.task_type = task_type;

    pthread_mutex_lock(&pool->pool_lock);
    if (pool->is_shutdown) {
        pthread_mutex_unlock(&pool->pool_lock);
        return -1; // Reject submissions during shutdown
    }
    pool->active_tasks_total++;
    pthread_mutex_unlock(&pool->pool_lock);

    // Ask the scheduler to select the target worker
    int target = scheduler_select_worker(pool, &task);
    if (target < 0 || target >= pool->num_threads) {
        target = 0;
    }

    worker_t *w = &pool->workers[target];

    pthread_mutex_lock(&w->lock);
    if (w->queue_count >= w->queue_capacity) {
        pthread_mutex_unlock(&w->lock);

        // Queue is full: decrement active counter and fail
        pthread_mutex_lock(&pool->pool_lock);
        pool->active_tasks_total--;
        pthread_mutex_unlock(&pool->pool_lock);
        return -1;
    }

    // Enqueue task into target worker's ring buffer
    w->queue[w->queue_tail] = task;
    w->queue_tail = (w->queue_tail + 1) % w->queue_capacity;
    w->queue_count++;

    // Wake the worker
    pthread_cond_signal(&w->has_work);
    pthread_mutex_unlock(&w->lock);

    return 0;
}

/**
 * Waits until all submitted tasks have completed
 */
void threadpool_wait(threadpool_t *pool) {
    if (!pool) return;

    pthread_mutex_lock(&pool->pool_lock);
    while (pool->active_tasks_total > 0) {
        pthread_cond_wait(&pool->all_idle, &pool->pool_lock);
    }
    pthread_mutex_unlock(&pool->pool_lock);
}

/**
 * Gracefully shuts down the thread pool
 */
void threadpool_destroy(threadpool_t *pool) {
    if (!pool) return;

    // 1. Signal shutdown
    pthread_mutex_lock(&pool->pool_lock);
    pool->is_shutdown = true;
    pthread_mutex_unlock(&pool->pool_lock);

    // 2. Wake all sleeping workers and signal shutdown under worker lock
    for (int i = 0; i < pool->num_threads; ++i) {
        worker_t *w = &pool->workers[i];
        pthread_mutex_lock(&w->lock);
        w->is_shutdown = true;
        pthread_cond_broadcast(&w->has_work);
        pthread_mutex_unlock(&w->lock);
    }

    // 3. Join all worker threads
    for (int i = 0; i < pool->num_threads; ++i) {
        pthread_join(pool->workers[i].thread, NULL);
    }

    // 4. Free resources for each worker
    for (int i = 0; i < pool->num_threads; ++i) {
        worker_t *w = &pool->workers[i];
        pthread_mutex_destroy(&w->lock);
        pthread_cond_destroy(&w->has_work);
        free(w->queue);
    }

    // 5. Clean up scheduler and pool allocations
    if (pool->scheduler_state) {
        free(pool->scheduler_state);
    }

    pthread_mutex_destroy(&pool->pool_lock);
    pthread_cond_destroy(&pool->all_idle);
    free(pool->workers);
    free(pool);
}

/**
 * Returns number of threads
 */
int threadpool_thread_count(threadpool_t *pool) {
    return pool ? pool->num_threads : 0;
}
