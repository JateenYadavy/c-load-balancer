#ifndef SERVER_H
#define SERVER_H

/**
 * ============================================================================
 * server.h - TCP Listening Socket & Accept Loop Declarations
 * ============================================================================
 *
 * PURPOSE:
 * This file declares the "front door" of our HTTP microservice.
 * It is responsible for:
 *
 * 1. Creating a TCP listening socket bound to a port.
 *    - Uses SO_REUSEADDR so we can restart the server immediately after crash.
 *    - Returns the raw socket file descriptor (int) on success.
 *
 * 2. Running the accept loop (server_run):
 *    - Blocks in a while(1) loop calling accept().
 *    - When a new client connects, accept() returns a client socket (client_fd).
 *    - That client_fd is passed directly to the thread pool as a task argument:
 *        threadpool_add(pool, http_handle_request, (void*)(intptr_t)client_fd, TASK_HTTP);
 *    - The thread pool and its scheduler then decide which worker handles it.
 *
 * HOW THIS CONNECTS TO THE TASK ASSIGNER:
 * The "work" for each worker thread is the HTTP handler function paired with
 * the client socket file descriptor. The worker reads the HTTP request from
 * that fd, computes a response, sends it, and closes the fd.
 *
 * KEY DESIGN DECISIONS:
 * - The accept loop runs in the MAIN THREAD (not a worker).
 *   Workers only handle existing connections, not accept new ones.
 * - client_fd is cast to (void*) via intptr_t (the portable way to pass
 *   an integer as a void pointer without losing bits on 64-bit systems).
 * ============================================================================
 */

#include "threadpool.h"

// Default port the HTTP server listens on
#define SERVER_PORT 8080

// Maximum number of pending connections in the OS kernel queue
#define SERVER_BACKLOG 128

// Task type ID for HTTP requests (used by Predictive Scheduler EMA table)
#define TASK_TYPE_HTTP 0

/**
 * Creates a TCP listening socket bound to the given port.
 * @param port Port number to bind to (e.g. 8080).
 * @return A valid server file descriptor (>= 0) on success, -1 on failure.
 */
int server_create(int port);

/**
 * Starts the accept loop and feeds incoming connections to the thread pool.
 * Blocks indefinitely (run in main thread).
 * @param server_fd The listening socket created by server_create().
 * @param pool      The initialized thread pool to dispatch tasks to.
 */
void server_run(int server_fd, threadpool_t *pool);

#endif // SERVER_H
