#ifndef PS5TORRENT_UI_H
#define PS5TORRENT_UI_H

#include <stddef.h>
#include "torrent.h"
#include "piece_mgr.h"

/**
 * Initialize the display/logging system.
 * Uses klog for kernel logging output (visible via serial/kernel log).
 */
void ui_init(void);

/**
 * Print a welcome/startup banner.
 */
void ui_print_banner(void);

/**
 * Print torrent information.
 */
void ui_print_torrent_info(const torrent_t *torrent);

/**
 * Print current download status.
 */
void ui_print_progress(const torrent_t *torrent, const piece_mgr_t *pm,
                       int active_peers, int64_t downloaded,
                       int64_t uploaded);

/**
 * Print a message to the log.
 */
void ui_log(const char *fmt, ...);

/**
 * Print an error message.
 */
void ui_error(const char *fmt, ...);

/**
 * Print a success message.
 */
void ui_success(const char *fmt, ...);

/**
 * Show a visible PS5 system notification.
 */
void ui_notify(const char *fmt, ...);

#endif /* PS5TORRENT_UI_H */
