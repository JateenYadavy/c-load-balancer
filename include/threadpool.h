#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Function pointer signature for any task
typedef void (*task_fn)(void *arg);

// Task definition: function pointer + arguments
typedef struct {
    task_fn function;
    void *arg;
} threadpool_task_t;

// Opaque ThreadPool handle
typedef struct threadpool threadpool_t;

/**
 * Creates a thread pool.
 * @param num_threads Number of worker threads. Pass 0 to auto-detect system CPU cores.
 * @param queue_capacity Maximum capacity of the task queue.
 * @return Pointer to threadpool_t on success, NULL on failure.
 */
threadpool_t* threadpool_create(int num_threads, int queue_capacity);

/**
 * Submits a new task to the thread pool queue.
 * @param pool Pointer to the thread pool.
 * @param function Function to execute.
 * @param arg Argument passed to the function.
 * @return 0 on success, -1 on failure (e.g. queue full or shutting down).
 */
int threadpool_add(threadpool_t *pool, task_fn function, void *arg);

/**
 * Blocks until all submitted tasks in the queue have finished executing.
 * @param pool Pointer to the thread pool.
 */
void threadpool_wait(threadpool_t *pool);

/**
 * Gracefully shuts down the thread pool, joins all worker threads,
 * and releases all allocated memory.
 * @param pool Pointer to the thread pool.
 */
void threadpool_destroy(threadpool_t *pool);

/**
 * Returns the number of worker threads in the pool.
 */
int threadpool_thread_count(threadpool_t *pool);

#ifdef __cplusplus
}
#endif

#endif // THREADPOOL_H
