/**
 * ============================================================================
 * scheduler.c - Task Dispatching Logic & Policy Implementation
 * ============================================================================
 */

#include "scheduler.h"
#include "threadpool_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <float.h>

/**
 * Initializes scheduler state (round-robin cursor and predictive EMA table)
 */
void scheduler_init(threadpool_t *pool) {
    if (!pool) return;

    scheduler_state_t *state = (scheduler_state_t *)calloc(1, sizeof(scheduler_state_t));
    if (!state) return;

    state->next_round_robin_worker = 0;
    pthread_mutex_init(&state->stats_lock, NULL);

    for (int i = 0; i < MAX_TASK_TYPES; ++i) {
        state->predicted_duration_ms[i] = 0.0; // 0.0 indicates unprofiled task type
    }

    pool->scheduler_state = state;
}

/**
 * Policy 1: Round-Robin Selection
 * Rotates sequentially through workers regardless of current backlog.
 */
static int select_round_robin(threadpool_t *pool) {
    scheduler_state_t *state = pool->scheduler_state;

    pthread_mutex_lock(&state->stats_lock);
    int chosen = state->next_round_robin_worker;
    state->next_round_robin_worker = (state->next_round_robin_worker + 1) % pool->num_threads;
    pthread_mutex_unlock(&state->stats_lock);

    return chosen;
}

/**
 * Policy 2: Least-Loaded Selection
 * Selects the worker with the lowest in-flight workload (queued + running).
 */
static int select_least_loaded(threadpool_t *pool) {
    int best_worker = 0;
    int min_load = 0;

    pthread_mutex_lock(&pool->workers[0].lock);
    min_load = pool->workers[0].queue_count + pool->workers[0].is_busy;
    pthread_mutex_unlock(&pool->workers[0].lock);

    for (int i = 1; i < pool->num_threads; ++i) {
        pthread_mutex_lock(&pool->workers[i].lock);
        int load = pool->workers[i].queue_count + pool->workers[i].is_busy;
        pthread_mutex_unlock(&pool->workers[i].lock);

        if (load < min_load) {
            min_load = load;
            best_worker = i;
        }
    }

    return best_worker;
}

/**
 * Policy 3: Predictive Selection
 * Estimates total remaining completion time (backlog ms) using Exponential Moving Average.
 */
static int select_predictive(threadpool_t *pool) {
    scheduler_state_t *state = pool->scheduler_state;
    int best_worker = 0;
    double min_backlog = DBL_MAX;

    pthread_mutex_lock(&state->stats_lock);

    for (int i = 0; i < pool->num_threads; ++i) {
        worker_t *w = &pool->workers[i];
        double backlog_ms = 0.0;

        pthread_mutex_lock(&w->lock);

        // Sum predicted duration of all tasks queued for this worker
        for (int q = 0; q < w->queue_count; ++q) {
            int idx = (w->queue_head + q) % w->queue_capacity;
            int type = w->queue[idx].task_type;
            if (type >= 0 && type < MAX_TASK_TYPES) {
                double est = state->predicted_duration_ms[type];
                backlog_ms += (est > 0.0) ? est : 10.0; // 10ms fallback for new task types
            }
        }

        // Add estimated remaining duration of the currently executing task
        if (w->is_busy) {
            int cur_type = w->current_task_type;
            if (cur_type >= 0 && cur_type < MAX_TASK_TYPES) {
                double cur_est = state->predicted_duration_ms[cur_type];
                backlog_ms += (cur_est > 0.0) ? (cur_est * 0.5) : 5.0; // Half remaining on average
            }
        }

        pthread_mutex_unlock(&w->lock);

        if (backlog_ms < min_backlog) {
            min_backlog = backlog_ms;
            best_worker = i;
        }
    }

    pthread_mutex_unlock(&state->stats_lock);
    return best_worker;
}

/**
 * Routes an incoming task to a worker according to the active policy
 */
int scheduler_select_worker(threadpool_t *pool, const task_t *task) {
    (void)task;
    if (!pool) return 0;

    switch (pool->policy) {
        case SCHEDULE_ROUND_ROBIN:
            return select_round_robin(pool);

        case SCHEDULE_LEAST_LOADED:
            return select_least_loaded(pool);

        case SCHEDULE_PREDICTIVE:
            return select_predictive(pool);

        default:
            return 0;
    }
}

/**
 * Updates the Exponential Moving Average (EMA) runtime history for a task type
 */
void scheduler_record_completion(threadpool_t *pool, int task_type, double duration_ms) {
    if (!pool || !pool->scheduler_state) return;
    if (task_type < 0 || task_type >= MAX_TASK_TYPES) return;

    scheduler_state_t *state = pool->scheduler_state;

    pthread_mutex_lock(&state->stats_lock);

    if (state->predicted_duration_ms[task_type] == 0.0) {
        // First run: establish initial baseline
        state->predicted_duration_ms[task_type] = duration_ms;
    } else {
        // Subsequent runs: apply EMA formula
        state->predicted_duration_ms[task_type] = 
            (1.0 - EMA_ALPHA) * state->predicted_duration_ms[task_type] + (EMA_ALPHA * duration_ms);
    }

    pthread_mutex_unlock(&state->stats_lock);
}
