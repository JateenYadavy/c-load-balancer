#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

/**
 * ============================================================================
 * http_handler.h - HTTP Request Handler Declaration
 * ============================================================================
 *
 * PURPOSE:
 * Declares http_handle_request(), the function pointer we pass as "work"
 * to the thread pool for every HTTP client connection.
 *
 * Each worker thread unpacks client_fd from the void* arg, reads the HTTP
 * request, and writes back a JSON response with timing & core information.
 * ============================================================================
 */

/**
 * HTTP request handler - executed by a worker thread on a CPU core.
 * @param arg  client_fd packed as (void*)(intptr_t)fd.
 */
void http_handle_request(void *arg);

#endif // HTTP_HANDLER_H
