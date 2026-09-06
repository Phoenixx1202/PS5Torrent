#include "console_files.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static int torrent_name(const char *s)
{
    size_t n = strlen(s);
    return n > 8 && !strcasecmp(s + n - 8, ".torrent");
}

static void escape(const char *s, char *out)
{
    const char *hex = "0123456789abcdef";
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c == '"' || c == '\\') { *out++ = '\\'; *out++ = c; }
        else if (c < 32) {
            memcpy(out, "\\u00", 4); out += 4;
            *out++ = hex[c >> 4]; *out++ = hex[c & 15];
        } else *out++ = c;
    }
    *out = 0;
}

http_response_t *console_files_list(const http_request_t *req)
{
    char path[1024] = "/", offset_text[24] = "0", resolved[4096], directories[8] = "";
    http_get_param(req->query, "directories", directories, sizeof(directories));
    int directories_only = !strcmp(directories, "1");
    if (!http_get_param(req->query, "path", path, sizeof(path))) strcpy(path, "/");
    http_get_param(req->query, "offset", offset_text, sizeof(offset_text));
    long offset = strtol(offset_text, NULL, 10);
    if (offset < 0) offset = 0;
    DIR *dir = path[0] == '/' && realpath(path, resolved) ? opendir(resolved) : NULL;
    if (!dir) {
        const char *body = "{\"error\":\"Cannot read this directory\"}";
        return http_response_new(403, "application/json", body, strlen(body));
    }
    char *json = malloc(262144);
    if (!json) { closedir(dir); return http_response_new(500, "application/json", "{}", 2); }
    char escaped[24577];
    escape(resolved, escaped);
    size_t pos = (size_t)snprintf(json, 262144, "{\"path\":\"%s\",\"writable\":%s,\"entries\":[", escaped,
                                access(resolved, W_OK) == 0 ? "true" : "false");
    struct dirent *entry;
    long seen = 0;
    int count = 0, more = 0;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child[4096]; struct stat st;
        int n = snprintf(child, sizeof(child), "%s/%s", resolved, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(child) || stat(child, &st)) continue;
        int directory = S_ISDIR(st.st_mode);
        if (directories_only && !directory) continue;
        if (!directory && !(S_ISREG(st.st_mode) && torrent_name(entry->d_name))) continue;
        if (seen++ < offset) continue;
        if (count == 128) { more = 1; break; }
        escape(entry->d_name, escaped);
        pos += (size_t)snprintf(json + pos, 262144 - pos,
            "%s{\"name\":\"%s\",\"directory\":%s}", count ? "," : "", escaped, directory ? "true" : "false");
        count++;
    }
    closedir(dir);
    pos += (size_t)snprintf(json + pos, 262144 - pos, "],\"next\":%ld}", more ? offset + count : -1L);
    http_response_t *response = http_response_new(200, "application/json", json, pos);
    free(json);
    return response;
}

static http_response_t *folder_error(int status, const char *message)
{
    char json[256];
    int n = snprintf(json, sizeof(json), "{\"error\":\"%s\"}", message);
    return http_response_new(status, "application/json", json, (size_t)n);
}

http_response_t *console_files_mkdir(const http_request_t *req)
{
    if (req->method != HTTP_POST || !req->body)
        return folder_error(405, "POST required");
    char parent[2048] = "", name[2048] = "", resolved[4096], child[4096];
    http_get_param(req->body, "path", parent, sizeof(parent));
    http_get_param(req->body, "name", name, sizeof(name));
    size_t length = strlen(name);
    if (!length || length > 255 || !strcmp(name, ".") || !strcmp(name, ".."))
        return folder_error(400, "Nome de pasta inválido.");
    for (size_t i = 0; i < length; i++)
        if (name[i] == '/' || name[i] == '\\' || (unsigned char)name[i] < 32 || name[i] == 127)
            return folder_error(400, "Nome de pasta inválido.");
    if (parent[0] != '/' || !realpath(parent, resolved))
        return folder_error(403, "Não foi possível abrir esta pasta.");
    int n = snprintf(child, sizeof(child), "%s%s%s", resolved,
                     !strcmp(resolved, "/") ? "" : "/", name);
    if (n < 0 || (size_t)n >= sizeof(child))
        return folder_error(400, "Caminho muito longo.");
    DIR *dir = opendir(resolved);
    if (!dir) return folder_error(403, "Não foi possível abrir esta pasta.");
    int result = mkdirat(dirfd(dir), name, 0755);
    int saved_errno = errno;
    closedir(dir);
    if (result)
        return folder_error(saved_errno == EEXIST ? 409 : 403,
                            saved_errno == EEXIST ? "Já existe um item com esse nome." : "Não foi possível criar a pasta.");
    char escaped[24577], json[24640];
    escape(child, escaped);
    n = snprintf(json, sizeof(json), "{\"success\":true,\"path\":\"%s\"}", escaped);
    return http_response_new(200, "application/json", json, (size_t)n);
}

unsigned char *console_torrent_read(const char *path, size_t *size)
{
    if (!path || path[0] != '/' || !torrent_name(path)) return NULL;
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > HTTP_MAX_BODY) {
        close(fd); return NULL;
    }
    unsigned char *data = malloc((size_t)st.st_size);
    if (!data) { close(fd); return NULL; }
    size_t used = 0;
    while (used < (size_t)st.st_size) {
        ssize_t n = read(fd, data + used, (size_t)st.st_size - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { free(data); close(fd); return NULL; }
        used += (size_t)n;
    }
    close(fd); *size = used;
    return data;
}
