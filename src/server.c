/**
 * ============================================================================
 * server.c - TCP Listening Socket & Accept Loop Implementation
 * ============================================================================
 */

#include "server.h"
#include "http_handler.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

/**
 * Creates a TCP listening socket bound to the given port.
 */
int server_create(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket() failed");
        return -1;
    }

    // Allow immediate reuse of port after server restart (avoids "Address in use" errors)
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind to all interfaces on the given port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind() failed");
        close(server_fd);
        return -1;
    }

    // Start listening (OS queues up to SERVER_BACKLOG pending connections)
    if (listen(server_fd, SERVER_BACKLOG) < 0) {
        perror("listen() failed");
        close(server_fd);
        return -1;
    }

    printf("[Server] Listening on port %d...\n", port);
    return server_fd;
}

/**
 * Accept loop: waits for clients and dispatches to thread pool.
 * Runs in the main thread indefinitely.
 */
void server_run(int server_fd, threadpool_t *pool) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    long total_connections = 0;

    while (1) {
        // Blocks until a client connects
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue; // Signal interrupted, retry
            perror("accept() failed");
            break;
        }

        total_connections++;

        // Hand off to the thread pool scheduler (the ONLY job of the accept loop!)
        // The client_fd integer is packed into a void* using intptr_t
        if (threadpool_add(pool, http_handle_request,
                           (void *)(intptr_t)client_fd,
                           TASK_TYPE_HTTP) != 0) {
            // Queue full: reject gracefully with HTTP 503
            const char *busy = "HTTP/1.1 503 Service Unavailable\r\n"
                               "Content-Length: 0\r\n\r\n";
            send(client_fd, busy, strlen(busy), 0);
            close(client_fd);
        }
    }
}
