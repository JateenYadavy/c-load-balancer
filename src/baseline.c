/**
 * ============================================================================
 * baseline.c - Naive Comparison: on-demand pthread_create() per connection
 * ============================================================================
 *
 * PURPOSE:
 * This binary answers: "What if we just let Linux do everything?"
 *
 * For every incoming HTTP connection we call pthread_create() fresh.
 * - No thread pool
 * - No CPU affinity / core pinning
 * - No queue, no scheduler
 * The OS schedules threads wherever it wants, with full context-switch overhead.
 *
 * We benchmark BOTH binaries with `ab` and compare:
 *   - Requests/second (throughput)
 *   - Latency p50 / p99 (responsiveness)
 * ============================================================================
 */

#define _GNU_SOURCE
#include "http_handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>

#define BASELINE_PORT    8081   // Different port so it can run alongside load_balancer
#define SERVER_BACKLOG   128

static volatile int g_running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    g_running = 0;
    printf("\n[Baseline] Shutting down...\n");
    _exit(0);
}

/* Thread entry: handle one client then immediately exit */
static void *thread_entry(void *arg) {
    pthread_detach(pthread_self()); // No join needed — fire and forget
    http_handle_request(arg);      // Reuse our existing handler
    return NULL;
}

int main(void) {
    printf("====================================================\n");
    printf(" BASELINE: On-demand pthread_create() per request   \n");
    printf(" No pool, no affinity, pure Linux scheduler         \n");
    printf(" Port: %d                                           \n", BASELINE_PORT);
    printf("====================================================\n\n");

    signal(SIGINT, handle_sigint);

    // Create listening socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(BASELINE_PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(server_fd); return 1;
    }
    if (listen(server_fd, SERVER_BACKLOG) < 0) {
        perror("listen"); close(server_fd); return 1;
    }

    printf("[Baseline] Listening on port %d...\n", BASELINE_PORT);
    printf("[Baseline] Test with:\n");
    printf("           curl http://localhost:%d/status\n", BASELINE_PORT);
    printf("           ab -n 5000 -c 50 http://localhost:%d/status\n\n", BASELINE_PORT);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (g_running) {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // Naive: spawn a brand new OS thread for every single request
        pthread_t tid;
        if (pthread_create(&tid, NULL, thread_entry, (void *)(intptr_t)client_fd) != 0) {
            // Thread creation failed: send 503 and move on
            const char *busy = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n";
            send(client_fd, busy, strlen(busy), 0);
            close(client_fd);
        }
        // Thread detaches itself; no pthread_join needed
    }

    close(server_fd);
    return 0;
}
