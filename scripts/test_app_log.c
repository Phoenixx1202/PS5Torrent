#include "app_log.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void *writer(void *unused)
{
    (void)unused;
    for (int i = 0; i < 200; i++) app_log_write("INFO", "Concurrent worker event");
    return NULL;
}
int main(void)
{
    char root[] = "/tmp/ps5torrent-log-XXXXXX"; assert(mkdtemp(root));
    app_log_write("INFO", "Early startup event");
    assert(app_log_open("relative") == -1);
    assert(!app_log_open(root));
    size_t length; char *text = app_log_read(&length);
    assert(text && strstr(text, "Early startup event") && strstr(text, "UTC")); free(text);
    pthread_t threads[4];
    for (int i = 0; i < 4; i++) assert(!pthread_create(&threads[i], NULL, writer, NULL));
    for (int i = 0; i < 4; i++) pthread_join(threads[i], NULL);
    char large[1024]; memset(large, 'x', sizeof(large) - 1); large[sizeof(large) - 1] = 0;
    for (int i = 0; i < 1300; i++) app_log_write("INFO", large);
    app_log_write("ERROR", "Final event\nnot a forged line");
    text = app_log_read(&length);
    assert(text && length <= 64 * 1024 && strstr(text, "Final event not a forged line")); free(text);
    app_log_close();
    char path[512], previous[512]; struct stat st;
    snprintf(path, sizeof(path), "%s/PS5Torrent.log", root);
    snprintf(previous, sizeof(previous), "%s/PS5Torrent.previous.log", root);
    assert(!stat(path, &st) && st.st_size > 0 && st.st_size <= 1024 * 1024);
    assert(!stat(previous, &st) && st.st_size > 0 && st.st_size <= 1024 * 1024);
    unlink(path); unlink(previous); rmdir(root);
    puts("Application log: startup buffering, persistence, concurrent writers, rotation and bounded tail passed");
}
