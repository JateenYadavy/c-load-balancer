#ifndef THREADPOOL_H
#define THREADPOOL_H

/**
 * ============================================================================
 * threadpool.h - Public Interface for the CPU Task Assigner & Thread Pool
 * ============================================================================
 * 
 * WHAT WE WANT IN THIS FILE:
 * 1. Policy Definitions (The 3 Schedulers we designed):
 *    - SCHEDULE_ROUND_ROBIN: Strict rotation across workers (Task i -> Worker i % N).
 *    - SCHEDULE_LEAST_LOADED: Dispatches to the worker with the lowest in-flight task count.
 *    - SCHEDULE_PREDICTIVE: Dispatches based on learned Exponential Moving Average (EMA)
 *      task durations to minimize total expected backlog latency.
 * 
 * 2. Task Signature and Data Structure:
 *    - task_fn: Function pointer signature for any unit of work `void (*)(void*)`.
 *    - task_t: Encapsulates function pointer, arguments, and a `task_type` ID used by
 *      the Predictive Scheduler to track and learn runtime durations.
 * 
 * 3. Opaque Handle:
 *    - threadpool_t: Keeps internal structures (mutexes, arrays, queues) private and safe
 *      from outside modification.
 * 
 * 4. Lifecycle & Control Functions:
 *    - threadpool_create(): Allocates resources, spawns worker threads, sets CPU affinity.
 *    - threadpool_add(): Enqueues a task according to the selected scheduling policy.
 *    - threadpool_wait(): Blocks until all submitted tasks finish executing.
 *    - threadpool_destroy(): Initiates graceful shutdown (broadcast -> join -> free).
 *    - threadpool_thread_count(): Returns the number of worker threads.
 * ============================================================================
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The 3 Scheduling Policies we support
typedef enum {
    SCHEDULE_ROUND_ROBIN,    // Policy 1: Strict rotation across workers
    SCHEDULE_LEAST_LOADED,   // Policy 2: Dynamic in-flight count (queued + running)
    SCHEDULE_PREDICTIVE      // Policy 3: Runtime-learned backlog time estimation (EMA)
} schedule_policy_t;

// Function pointer signature for tasks
typedef void (*task_fn)(void *arg);

// Unit of work representation
typedef struct {
    task_fn function;        // Code to execute
    void *arg;               // Data passed to the function
    int task_type;           // Identifier used by the predictive scheduler (e.g. 0, 1, 2)
} task_t;

// Opaque thread pool handle (implementation details hidden in threadpool.c)
typedef struct threadpool threadpool_t;

/**
 * Creates and initializes the thread pool.
 * @param num_threads Number of worker threads. Pass 0 to auto-detect system CPU cores.
 * @param queue_capacity Maximum capacity of task queue buffers.
 * @param policy Scheduling policy (SCHEDULE_ROUND_ROBIN, SCHEDULE_LEAST_LOADED, SCHEDULE_PREDICTIVE).
 * @return Pointer to threadpool_t on success, NULL on failure.
 */
threadpool_t* threadpool_create(int num_threads, int queue_capacity, schedule_policy_t policy);

/**
 * Submits a new task to the thread pool according to the active scheduling policy.
 * @param pool Pointer to the thread pool.
 * @param function Function to execute.
 * @param arg Argument passed to the function.
 * @param task_type Identifier for the task (used by predictive scheduler).
 * @return 0 on success, -1 on failure (e.g. queue full or pool shutting down).
 */
int threadpool_add(threadpool_t *pool, task_fn function, void *arg, int task_type);

/**
 * Blocks the calling thread until all submitted tasks have finished processing.
 * @param pool Pointer to the thread pool.
 */
void threadpool_wait(threadpool_t *pool);

/**
 * Performs a graceful shutdown:
 * 1. Rejects any new tasks.
 * 2. Allows running/queued tasks to drain.
 * 3. Wakes all sleeping workers via pthread_cond_broadcast.
 * 4. Joins all threads and frees all heap memory.
 * @param pool Pointer to the thread pool.
 */
void threadpool_destroy(threadpool_t *pool);

/**
 * Returns the number of active worker threads in the pool.
 */
int threadpool_thread_count(threadpool_t *pool);

#ifdef __cplusplus
}
#endif

#endif // THREADPOOL_H
