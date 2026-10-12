#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "threadpool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes scheduler state (round-robin counters, EMA tables).
 */
void scheduler_init(threadpool_t *pool);

/**
 * Chooses which worker thread should receive the incoming task based on the pool's policy:
 * - SCHEDULE_ROUND_ROBIN
 * - SCHEDULE_LEAST_LOADED
 * - SCHEDULE_PREDICTIVE
 * 
 * @return Index of the chosen worker (0 to num_threads - 1).
 */
int scheduler_select_worker(threadpool_t *pool, const task_t *task);

/**
 * Records execution duration for a task type to update the Predictive Scheduler's EMA table.
 */
void scheduler_record_completion(threadpool_t *pool, int task_type, double duration_ms);

#ifdef __cplusplus
}
#endif

#endif // SCHEDULER_H
