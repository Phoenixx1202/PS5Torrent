#include "app_log.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#define LOG_LIMIT (1024 * 1024)
#define LOG_TAIL (64 * 1024)
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *output;
static char log_path[1024], previous_path[1024];
static char recent[LOG_TAIL];
static size_t recent_length, file_length;
static int persistence_error;

static void remember(const char *line, size_t length)
{
    if (length > sizeof(recent)) { line += length - sizeof(recent); length = sizeof(recent); }
    if (recent_length + length > sizeof(recent)) {
        size_t discard = recent_length + length - sizeof(recent);
        memmove(recent, recent + discard, recent_length - discard);
        recent_length -= discard;
    }
    memcpy(recent + recent_length, line, length);
    recent_length += length;
}

static int rotate(void)
{
    if (output) { fclose(output); output = NULL; }
    if (rename(log_path, previous_path) != 0 && errno != ENOENT) return -1;
    output = fopen(log_path, "ab");
    file_length = 0;
    return output ? 0 : -1;
}

static void persist(const char *line, size_t length)
{
    if (!output) return;
    if (file_length + length > LOG_LIMIT && rotate()) { persistence_error = errno; return; }
    if (fwrite(line, 1, length, output) != length || fflush(output)) {
        persistence_error = errno ? errno : EIO;
        fclose(output); output = NULL;
        return;
    }
    file_length += length;
}

int app_log_open(const char *directory)
{
    pthread_mutex_lock(&lock);
    if (output) { pthread_mutex_unlock(&lock); return 0; }
    if (!directory || directory[0] != '/' || strlen(directory) > sizeof(log_path) - 32) {
        persistence_error = EINVAL; pthread_mutex_unlock(&lock); return -1;
    }
    snprintf(log_path, sizeof(log_path), "%s/PS5Torrent.log", directory);
    snprintf(previous_path, sizeof(previous_path), "%s/PS5Torrent.previous.log", directory);
    if (mkdir(directory, 0755) && errno != EEXIST) {
        persistence_error = errno; pthread_mutex_unlock(&lock); return -1;
    }
    struct stat st;
    file_length = !stat(log_path, &st) && st.st_size > 0 ? (size_t)st.st_size : 0;
    output = fopen(log_path, "ab");
    persistence_error = output ? 0 : errno;
    if (output) persist(recent, recent_length);
    int ok = output != NULL;
    pthread_mutex_unlock(&lock);
    return ok ? 0 : -1;
}

void app_log_write(const char *level, const char *message)
{
    char line[2048], date[32];
    time_t now = time(NULL); struct tm utc;
    gmtime_r(&now, &utc);
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S UTC", &utc);
    int n = snprintf(line, sizeof(line), "[%s] [%s] %s", date, level, message ? message : "");
    size_t length = n < 0 ? 0 : (size_t)n;
    if (length >= sizeof(line) - 1) length = sizeof(line) - 2;
    /* Keep each event on one line, including user-provided torrent names. */
    for (size_t i = 0; i < length; i++) if ((unsigned char)line[i] < 32) line[i] = ' ';
    line[length++] = '\n';
    pthread_mutex_lock(&lock);
    remember(line, length);
    persist(line, length);
    pthread_mutex_unlock(&lock);
}

char *app_log_read(size_t *length)
{
    char *text = malloc(LOG_TAIL + 256);
    if (!text) return NULL;
    pthread_mutex_lock(&lock);
    size_t used = 0;
    FILE *file = output ? fopen(log_path, "rb") : NULL;
    if (file) {
        if (fseek(file, 0, SEEK_END) == 0) {
            long size = ftell(file);
            long start = size > LOG_TAIL ? size - LOG_TAIL : 0;
            if (size >= 0 && fseek(file, start, SEEK_SET) == 0) {
                if (start) { int c; while ((c = fgetc(file)) != '\n' && c != EOF) {} }
                used = fread(text, 1, LOG_TAIL, file);
            }
        }
        fclose(file);
    } else {
        used = (size_t)snprintf(text, 256,
            "Persistent log unavailable (error %d). Showing in-memory events.\n", persistence_error);
    }
    if (!file || !used) { memcpy(text + used, recent, recent_length); used += recent_length; }
    text[used] = 0;
    pthread_mutex_unlock(&lock);
    *length = used;
    return text;
}

void app_log_close(void)
{
    pthread_mutex_lock(&lock);
    if (output) { fclose(output); output = NULL; }
    pthread_mutex_unlock(&lock);
}
