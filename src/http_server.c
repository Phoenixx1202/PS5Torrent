#include "http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

/**
 * Maximum registered handlers.
 */
#define MAX_HANDLERS 16

/**
 * Maximum client connections.
 */
#define MAX_CLIENTS 8

/**
 * Registered handler entry.
 */
typedef struct {
    char            path[128];
    http_handler_t  handler;
} handler_entry_t;

/* Static state */
static int                server_fd = -1;
static handler_entry_t    handlers[MAX_HANDLERS];
static int                num_handlers = 0;
static int                client_socks[MAX_CLIENTS];
static int                num_clients = 0;

int http_server_init(uint16_t port)
{
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    if (listen(server_fd, 4) < 0) {
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    return 0;
}

void http_server_register(const char *path, http_handler_t handler)
{
    if (num_handlers >= MAX_HANDLERS) return;
    strncpy(handlers[num_handlers].path, path, sizeof(handlers[num_handlers].path) - 1);
    handlers[num_handlers].handler = handler;
    num_handlers++;
}

int http_server_get_fd(void)
{
    return server_fd;
}

void http_url_decode(char *str)
{
    char *src = str;
    char *dst = str;
    while (*src) {
        if (*src == '%' && *(src + 1) && *(src + 2)) {
            char hex[3] = {src[1], src[2], 0};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

int http_get_param(const char *query, const char *key,
                   char *value, size_t val_len)
{
    if (!query || !key || !value) return 0;

    size_t key_len = strlen(key);
    const char *p = query;

    while (*p) {
        /* Skip leading & */
        if (*p == '&') p++;
        if (!*p) break;

        /* Check if this param starts with our key */
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            p += key_len + 1;
            size_t copied = 0;
            while (*p && *p != '&' && copied < val_len - 1) {
                value[copied++] = *p++;
            }
            value[copied] = '\0';
            http_url_decode(value);
            return 1;
        }

        /* Skip to next param */
        while (*p && *p != '&') p++;
    }

    return 0;
}

static const char *find_bytes(const char *haystack, size_t haystack_len,
                              const char *needle, size_t needle_len)
{
    if (!haystack || !needle || needle_len == 0 || needle_len > haystack_len)
        return NULL;

    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (haystack[i] == needle[0] &&
            memcmp(haystack + i, needle, needle_len) == 0)
            return haystack + i;
    }
    return NULL;
}

static const char *multipart_data_start(const char *field, const char *end)
{
    const char *blank = find_bytes(field, (size_t)(end - field), "\r\n\r\n", 4);
    if (blank) return blank + 4;
    blank = find_bytes(field, (size_t)(end - field), "\n\n", 2);
    return blank ? blank + 2 : NULL;
}

int http_get_upload(const char *body, size_t body_len,
                    const char *field_name,
                    const char **file_data, size_t *file_len,
                    char *filename, size_t filename_len)
{
    /* Simple multipart/form-data parser */
    /* Find Content-Disposition header for the field */
    const char *end = body + body_len;
    char search[256];
    int n = snprintf(search, sizeof(search), "name=\"%s\"", field_name);
    if (n < 0 || (size_t)n >= sizeof(search)) return 0;

    const char *field_start = find_bytes(body, body_len, search, (size_t)n);
    if (!field_start) return 0;

    /* Look for filename if provided */
    const char *fn_start = find_bytes(field_start, (size_t)(end - field_start),
                                      "filename=\"", 10);
    const char *headers_end = multipart_data_start(field_start, end);
    if (fn_start && headers_end && fn_start < headers_end) {
        fn_start += 10; // skip 'filename="'
        const char *fn_end = find_bytes(fn_start, (size_t)(headers_end - fn_start),
                                        "\"", 1);
        if (fn_end && (size_t)(fn_end - fn_start) < filename_len) {
            size_t fn_len = (size_t)(fn_end - fn_start);
            memcpy(filename, fn_start, fn_len);
            filename[fn_len] = '\0';
        }
    }

    /* Find blank line (\r\n\r\n) after the headers */
    const char *blank = headers_end;
    if (!blank) return 0;

    /* Find boundary (next \r\n--...) */
    const char *boundary = find_bytes(blank, (size_t)(end - blank), "\r\n--", 4);

    const char *data_end = boundary ? boundary : end;
    if (data_end > blank) {
        *file_data = blank;
        *file_len = (size_t)(data_end - blank);
        return 1;
    }

    return 0;
}

int http_get_multipart_field(const char *body, size_t body_len,
                             const char *field_name,
                             char *value, size_t value_len)
{
    char search[256];
    int n;
    const char *end = body + body_len;
    const char *field;
    const char *data;
    const char *boundary;
    size_t length;

    if (!body || !field_name || !value || value_len == 0) return 0;
    value[0] = '\0';
    n = snprintf(search, sizeof(search), "name=\"%s\"", field_name);
    if (n < 0 || (size_t)n >= sizeof(search)) return 0;

    field = find_bytes(body, body_len, search, (size_t)n);
    if (!field) return 0;
    data = multipart_data_start(field, end);
    if (!data) return 0;
    boundary = find_bytes(data, (size_t)(end - data), "\r\n--", 4);
    if (!boundary) return 0;

    length = (size_t)(boundary - data);
    if (length >= value_len) length = value_len - 1;
    memcpy(value, data, length);
    value[length] = '\0';
    return 1;
}

static int parse_http_request(int sock, http_request_t *req)
{
    char buf[8192];
    ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return -1;
    buf[n] = '\0';

    memset(req, 0, sizeof(http_request_t));

    /* Parse request line: METHOD /path HTTP/1.x */
    char *line = buf;
    char *end_line = strstr(line, "\r\n");
    if (!end_line) return -1;
    *end_line = '\0';

    char *method = strtok(line, " ");
    char *path = strtok(NULL, " ");
    if (!method || !path) return -1;

    if (strcmp(method, "GET") == 0) req->method = HTTP_GET;
    else if (strcmp(method, "POST") == 0) req->method = HTTP_POST;
    else req->method = HTTP_UNSUPPORTED;

    /* Split path and query string */
    char *qmark = strchr(path, '?');
    if (qmark) {
        *qmark = '\0';
        strncpy(req->query, qmark + 1, sizeof(req->query) - 1);
    }
    strncpy(req->path, path, sizeof(req->path) - 1);
    http_url_decode(req->path);

    /* Parse headers to find Content-Type and Content-Length */
    char *headers = end_line + 2;
    char *body_start = NULL;

    /* Find blank line */
    char *blank = strstr(headers, "\r\n\r\n");
    if (blank) {
        body_start = blank + 4;

        /* Parse Content-Length */
        char *cl = strstr(headers, "Content-Length:");
        if (!cl) cl = strstr(headers, "content-length:");
        if (cl) {
            cl += 15;
            while (*cl == ' ') cl++;
            size_t clen = 0;
            while (*cl >= '0' && *cl <= '9') {
                clen = clen * 10 + (*cl++ - '0');
            }
            if (clen > HTTP_MAX_BODY) clen = HTTP_MAX_BODY;

            /* Copy body */
            req->body = malloc(clen + 1);
            if (req->body) {
                size_t avail = (size_t)(buf + n - body_start);
                size_t to_copy = clen < avail ? clen : avail;
                memcpy(req->body, body_start, to_copy);
                req->body[to_copy] = '\0';
                req->body_len = to_copy;

                /* If body is larger than what we've read, read more */
                if (to_copy < clen) {
                    char *more = realloc(req->body, clen + 1);
                    if (more) {
                        req->body = more;
                        size_t offset = to_copy;
                        while (offset < clen) {
                            ssize_t r = recv(sock, req->body + offset,
                                             clen - offset, 0);
                            if (r <= 0) break;
                            offset += (size_t)r;
                        }
                        req->body[offset] = '\0';
                        req->body_len = offset;
                    }
                }
            }
        }

        /* Parse Content-Type */
        char *ct = strstr(headers, "Content-Type:");
        if (!ct) ct = strstr(headers, "content-type:");
        if (ct) {
            ct += 13;
            while (*ct == ' ') ct++;
            char *ct_end = strstr(ct, "\r\n");
            size_t ct_len = ct_end ? (size_t)(ct_end - ct) : strlen(ct);
            if (ct_len >= sizeof(req->content_type))
                ct_len = sizeof(req->content_type) - 1;
            memcpy(req->content_type, ct, ct_len);
            req->content_type[ct_len] = '\0';
        }
    }

    return 0;
}

int http_server_poll(int timeout_ms)
{
    if (server_fd < 0) return -1;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(server_fd, &readfds);

    int max_fd = server_fd;

    /* Add client sockets */
    for (int i = 0; i < num_clients; i++) {
        if (client_socks[i] >= 0) {
            FD_SET(client_socks[i], &readfds);
            if (client_socks[i] > max_fd)
                max_fd = client_socks[i];
        }
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(max_fd + 1, &readfds, NULL, NULL,
                     timeout_ms >= 0 ? &tv : NULL);
    if (ret < 0) return -1;
    if (ret == 0) return 1; // Timeout

    /* Accept new connections */
    if (FD_ISSET(server_fd, &readfds)) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client = accept(server_fd,
                            (struct sockaddr *)&client_addr, &addr_len);
        if (client >= 0) {
            if (num_clients < MAX_CLIENTS) {
                client_socks[num_clients++] = client;
            } else {
                close(client);
            }
        }
    }

    /* Handle client requests */
    for (int i = 0; i < num_clients; i++) {
        if (client_socks[i] >= 0 && FD_ISSET(client_socks[i], &readfds)) {
            http_request_t req;
            if (parse_http_request(client_socks[i], &req) < 0) {
                close(client_socks[i]);
                client_socks[i] = -1;
                continue;
            }

            /* Find handler */
            http_response_t *resp = NULL;
            for (int h = 0; h < num_handlers; h++) {
                size_t plen = strlen(handlers[h].path);
                if (strncmp(req.path, handlers[h].path, plen) == 0) {
                    resp = handlers[h].handler(&req);
                    break;
                }
            }

            if (!resp) {
                /* 404 */
                const char *msg = "404 Not Found";
                resp = http_response_new(404, "text/plain", msg, strlen(msg));
            }

            http_server_send_response(client_socks[i], resp);
            http_response_free(resp);

            free(req.body);
            close(client_socks[i]);
            client_socks[i] = -1;
        }
    }

    /* Cleanup closed client slots */
    int write_idx = 0;
    for (int i = 0; i < num_clients; i++) {
        if (client_socks[i] >= 0)
            client_socks[write_idx++] = client_socks[i];
    }
    num_clients = write_idx;

    return 0;
}

void http_server_send_response(int sock, const http_response_t *resp)
{
    if (!resp) return;

    char header[1024];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-cache\r\n"
        "\r\n",
        resp->status_code,
        resp->status_code == 200 ? "OK" :
        resp->status_code == 404 ? "Not Found" :
        resp->status_code == 400 ? "Bad Request" : "Error",
        resp->content_type ? resp->content_type : "text/html",
        resp->body_len);

    send(sock, header, (size_t)n, 0);
    if (resp->body && resp->body_len > 0)
        send(sock, resp->body, resp->body_len, 0);
}

http_response_t *http_response_new(int status, const char *content_type,
                                    const char *body, size_t body_len)
{
    http_response_t *resp = calloc(1, sizeof(http_response_t));
    if (!resp) return NULL;

    resp->status_code = status;
    resp->content_type = strdup(content_type ? content_type : "text/plain");
    resp->body = malloc(body_len + 1);
    if (resp->body) {
        memcpy(resp->body, body, body_len);
        resp->body[body_len] = '\0';
    }
    resp->body_len = body_len;

    return resp;
}

void http_response_free(http_response_t *resp)
{
    if (!resp) return;
    free(resp->content_type);
    free(resp->body);
    free(resp);
}

void http_server_shutdown(void)
{
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    for (int i = 0; i < num_clients; i++) {
        if (client_socks[i] >= 0) {
            close(client_socks[i]);
            client_socks[i] = -1;
        }
    }
    num_clients = 0;
}
