/**
 * ============================================================================
 * http_handler.c - HTTP Request Parsing & Response Writer
 * ============================================================================
 *
 * This is the "work" that every worker thread executes per HTTP connection.
 *
 * Lifecycle per request:
 * 1. Unpack client_fd from void* arg.
 * 2. recv() the HTTP request bytes from the client socket.
 * 3. Parse the request line (method + path).
 * 4. Route to the correct handler:
 *    - GET /        -> Welcome JSON response
 *    - GET /ping    -> Health check response
 *    - GET /status  -> Server info (core ID, thread ID)
 *    - Anything else -> 404 Not Found
 * 5. send() the HTTP response.
 * 6. close() the client_fd.
 * ============================================================================
 */

#include "http_handler.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#ifdef __linux__
    #include <sched.h>
#endif

#define RECV_BUFFER_SIZE 4096
#define SEND_BUFFER_SIZE 4096

// Helper: sends a complete HTTP response
static void send_response(int fd, int status_code, const char *status_text,
                          const char *content_type, const char *body) {
    char header[512];
    int body_len = (int)strlen(body);

    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code, status_text, content_type, body_len);

    send(fd, header, header_len, 0);
    send(fd, body, body_len, 0);
}

// GET / -> Welcome message
static void handle_root(int client_fd) {
    const char *body =
        "{\n"
        "  \"service\": \"CPU Task Assigner HTTP Server\",\n"
        "  \"status\": \"ok\",\n"
        "  \"routes\": [\"/\", \"/ping\", \"/status\"]\n"
        "}\n";
    send_response(client_fd, 200, "OK", "application/json", body);
}

// GET /ping -> Health check
static void handle_ping(int client_fd) {
    send_response(client_fd, 200, "OK", "application/json",
                  "{\"pong\": true}\n");
}

// GET /status -> Core ID, thread ID, and timestamp
static void handle_status(int client_fd) {
    int core_id = -1;

#ifdef __linux__
    core_id = sched_getcpu();
#endif

    double ts = get_time_ms();
    char body[256];
    snprintf(body, sizeof(body),
        "{\n"
        "  \"status\": \"ok\",\n"
        "  \"core_id\": %d,\n"
        "  \"timestamp_ms\": %.3f\n"
        "}\n",
        core_id, ts);

    send_response(client_fd, 200, "OK", "application/json", body);
}

// 404 fallback
static void handle_not_found(int client_fd, const char *path) {
    char body[256];
    snprintf(body, sizeof(body),
        "{\"error\": \"not found\", \"path\": \"%s\"}\n", path);
    send_response(client_fd, 404, "Not Found", "application/json", body);
}

/**
 * Main task function executed by a worker thread for every HTTP request.
 */
void http_handle_request(void *arg) {
    int client_fd = (int)(intptr_t)arg;

    char buffer[RECV_BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    // Read the HTTP request (non-blocking: just one recv is fine for short requests)
    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        close(client_fd);
        return;
    }

    // Parse the first line: "GET /path HTTP/1.1"
    char method[16] = {0};
    char path[256]  = {0};
    sscanf(buffer, "%15s %255s", method, path);

    // Route based on path
    if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) {
        handle_root(client_fd);
    } else if (strcmp(path, "/ping") == 0) {
        handle_ping(client_fd);
    } else if (strcmp(path, "/status") == 0) {
        handle_status(client_fd);
    } else {
        handle_not_found(client_fd, path);
    }

    close(client_fd);
}
