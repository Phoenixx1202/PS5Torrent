#ifndef CONSOLE_FILES_H
#define CONSOLE_FILES_H
#include "http_server.h"
http_response_t *console_files_list(const http_request_t *req);
http_response_t *console_files_mkdir(const http_request_t *req);
/* Reads only regular .torrent files, bounded by HTTP_MAX_BODY. */
unsigned char *console_torrent_read(const char *path, size_t *size);
#endif
