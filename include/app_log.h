#ifndef APP_LOG_H
#define APP_LOG_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Early messages are buffered until filesystem access is ready. */
int app_log_open(const char *directory);
void app_log_write(const char *level, const char *message);
/* Caller frees the returned UTF-8 text. Includes a persistence warning if needed. */
char *app_log_read(size_t *length);
void app_log_close(void);

#ifdef __cplusplus
}
#endif

#endif
