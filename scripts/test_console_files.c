#include "console_files.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char root[] = "/tmp/ps5torrent-files-XXXXXX";
    assert(mkdtemp(root));
    char path[1024];
    snprintf(path, sizeof(path), "%s/a.torrent", root);
    FILE *f = fopen(path, "wb"); assert(f);
    const unsigned char bytes[] = {'d', 0, 'e'};
    assert(fwrite(bytes, 1, sizeof(bytes), f) == sizeof(bytes)); fclose(f);
    size_t size = 0;
    unsigned char *data = console_torrent_read(path, &size);
    assert(data && size == sizeof(bytes) && !memcmp(data, bytes, size)); free(data);
    assert(!console_torrent_read(root, &size));
    snprintf(path, sizeof(path), "%s/pipe.torrent", root);
    assert(!mkfifo(path, 0600)); assert(!console_torrent_read(path, &size)); unlink(path);
    snprintf(path, sizeof(path), "%s/large.torrent", root);
    f = fopen(path, "wb"); assert(f); assert(!ftruncate(fileno(f), HTTP_MAX_BODY + 1)); fclose(f);
    assert(!console_torrent_read(path, &size)); unlink(path);
    for (int i = 0; i < 130; i++) {
        snprintf(path, sizeof(path), "%s/folder-%d", root, i); assert(!mkdir(path, 0700));
    }
    http_request_t req = {0};
    snprintf(req.query, sizeof(req.query), "path=%s", root);
    http_response_t *response = console_files_list(&req);
    assert(response && response->status_code == 200);
    assert(strstr(response->body, "\"next\":128")); http_response_free(response);
    snprintf(req.query, sizeof(req.query), "path=%s&offset=128", root);
    response = console_files_list(&req);
    assert(response && response->status_code == 200 && strstr(response->body, "\"next\":-1"));
    http_response_free(response);
    snprintf(req.query, sizeof(req.query), "path=%s&directories=1", root);
    response = console_files_list(&req);
    assert(response->status_code == 200 && !strstr(response->body, "a.torrent"));
    http_response_free(response);
    char body[2048];
    snprintf(body, sizeof(body), "path=%s&name=New+Folder", root);
    req.method = HTTP_POST; req.body = body;
    response = console_files_mkdir(&req); assert(response->status_code == 200); http_response_free(response);
    snprintf(path, sizeof(path), "%s/New Folder", root);
    struct stat created; assert(!stat(path, &created) && S_ISDIR(created.st_mode));
    response = console_files_mkdir(&req); assert(response->status_code == 409); http_response_free(response);
    const char *invalid[] = {"..", ".", "..%2Foutside", "bad%5Cname", "%01bad", ""};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        snprintf(body, sizeof(body), "path=%s&name=%s", root, invalid[i]);
        response = console_files_mkdir(&req); assert(response->status_code == 400); http_response_free(response);
    }
    req.method = HTTP_GET;
    response = console_files_mkdir(&req); assert(response->status_code == 405); http_response_free(response);
    assert(!rmdir(path));
    snprintf(req.query, sizeof(req.query), "path=%s/missing", root);
    response = console_files_list(&req); assert(response->status_code == 403); http_response_free(response);
    for (int i = 0; i < 130; i++) {
        snprintf(path, sizeof(path), "%s/folder-%d", root, i); assert(!rmdir(path));
    }
    snprintf(path, sizeof(path), "%s/a.torrent", root); unlink(path); rmdir(root);
    puts("Console files: binary read, size limits, pagination, directory filter, mkdir, duplicate and invalid name rejection passed");
}
