/**
 * ============================================================================
 * main.c - Entry Point: Starts HTTP Server on top of CPU Task Assigner
 * ============================================================================
 */

#define _GNU_SOURCE
#include "threadpool.h"
#include "server.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

// Global pool pointer for signal handler cleanup
static threadpool_t *g_pool = NULL;
static int           g_server_fd = -1;

static void handle_sigint(int sig) {
    (void)sig;
    printf("\n[Server] Shutting down gracefully...\n");
    if (g_server_fd >= 0) close(g_server_fd);
    if (g_pool) threadpool_destroy(g_pool);
    printf("[Server] Done. Goodbye!\n");
    _exit(0);
}

int main(void) {
    int cores = get_cpu_core_count();

    printf("====================================================\n");
    printf(" HTTP Microservice using CPU Task Assigner in C     \n");
    printf(" Physical Cores Detected: %d                        \n", cores);
    printf("====================================================\n\n");

    // 1. Register Ctrl+C handler for graceful shutdown
    signal(SIGINT, handle_sigint);

    // 2. Create thread pool (one worker per CPU core, Least-Loaded policy)
    g_pool = threadpool_create(cores, 1024, SCHEDULE_LEAST_LOADED);
    if (!g_pool) {
        fprintf(stderr, "[Error] Failed to create thread pool.\n");
        return 1;
    }
    printf("[Server] Thread pool started: %d workers (LEAST_LOADED policy)\n", cores);

    // 3. Create TCP listening socket
    g_server_fd = server_create(SERVER_PORT);
    if (g_server_fd < 0) {
        threadpool_destroy(g_pool);
        return 1;
    }

    printf("[Server] Ready! Test with:\n");
    printf("         curl http://localhost:%d/\n", SERVER_PORT);
    printf("         curl http://localhost:%d/ping\n", SERVER_PORT);
    printf("         curl http://localhost:%d/status\n", SERVER_PORT);
    printf("         ab -n 10000 -c 100 http://localhost:%d/\n\n", SERVER_PORT);

    // 4. Start accept loop (blocks until SIGINT)
    server_run(g_server_fd, g_pool);

    return 0;
}
