#ifndef PS5TORRENT_HTTP_SERVER_H
#define PS5TORRENT_HTTP_SERVER_H

#include <stddef.h>
#include <stdint.h>

/**
 * HTTP server port.
 */
#define HTTP_PORT 8080

/**
 * Maximum request body size (for file uploads etc.).
 */
#define HTTP_MAX_BODY (1024 * 1024)  // 1MB

/**
 * HTTP request types.
 */
typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_UNSUPPORTED
} http_method_t;

/**
 * Parsed HTTP request.
 */
typedef struct {
    http_method_t method;
    char          path[1024];
    char          query[2048];
    char         *body;
    size_t        body_len;
    char          content_type[128];
} http_request_t;

/**
 * HTTP response to send.
 */
typedef struct {
    int   status_code;
    char *content_type;
    char *body;
    size_t body_len;
} http_response_t;

/**
 * Callback type for handling HTTP requests.
 * @param request  Parsed request
 * @return Response to send, or NULL for 404
 */
typedef http_response_t* (*http_handler_t)(const http_request_t *request);

/**
 * Initialize the HTTP server on the given port.
 * @param port  Port to listen on (e.g. 8080)
 * @return 0 on success, -1 on error
 */
int http_server_init(uint16_t port);

/**
 * Register a handler for a specific path prefix.
 * @param path     Path prefix (e.g. "/api/")
 * @param handler  Handler callback
 */
void http_server_register(const char *path, http_handler_t handler);

/**
 * Accept and handle one HTTP request (non-blocking with timeout).
 * @param timeout_ms  Max time to wait for a connection (0 = no wait)
 * @return 0 if request handled, 1 if timeout, -1 on error
 */
int http_server_poll(int timeout_ms);

/**
 * Get the server's file descriptor (for use with select()).
 */
int http_server_get_fd(void);

/**
 * Send a complete HTTP response.
 * @param sock     Client socket
 * @param response Response to send
 */
void http_server_send_response(int sock, const http_response_t *response);

/**
 * Create an HTTP response (caller must free with http_response_free).
 */
http_response_t *http_response_new(int status, const char *content_type,
                                    const char *body, size_t body_len);

/**
 * Free an HTTP response.
 */
void http_response_free(http_response_t *resp);

/**
 * URL-decode a percent-encoded string (in-place).
 */
void http_url_decode(char *str);

/**
 * Parse query string from a URL-encoded form body or query string.
 * @param query    The query string or form body
 * @param key      The key to find
 * @param value    Output buffer
 * @param val_len  Max length of value buffer
 * @return 1 if found, 0 if not
 */
int http_get_param(const char *query, const char *key,
                   char *value, size_t val_len);

/**
 * Get uploaded file data from multipart form data.
 * @param body         Request body
 * @param body_len     Body length
 * @param field_name   Form field name
 * @param file_data    Output: file data pointer (points into body)
 * @param file_len     Output: file data length
 * @param filename     Output: original filename
 * @return 1 if found, 0 if not
 */
int http_get_upload(const char *body, size_t body_len,
                    const char *field_name,
                    const char **file_data, size_t *file_len,
                    char *filename, size_t filename_len);

/**
 * Shutdown the HTTP server.
 */
void http_server_shutdown(void);

#endif /* PS5TORRENT_HTTP_SERVER_H */
