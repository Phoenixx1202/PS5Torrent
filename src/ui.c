#include "ui.h"
#include "app_log.h"
#include <stdio.h>
#include <stdarg.h>

/**
 * UI implementation using kernel logging (klog).
 * klog output is visible via serial console or kernel log tools on the PS5.
 *
 * In environments where klog is not available, we fall back to standard
 * printf which goes to the hijacked process's stdout (if any).
 */

/* Forward declaration of klog functions (from ps5-payload-sdk) */
extern int klog_printf(const char *fmt, ...);
extern int klog_puts(const char *s);

/* Flag set during ui_init */
static int use_klog = 0;

typedef struct {
    char reserved[45];
    char message[3075];
} notification_request_t;

extern int sceKernelSendNotificationRequest(int device,
                                             notification_request_t *request,
                                             size_t request_size,
                                             int flags);

void ui_init(void)
{
    /* Try to initialize klog - if it fails, fall back to printf */
    /* klog_printf will work after __klog_init() in the CRT */
    klog_printf("=== PS5Torrent Initializing ===\n");
    use_klog = 1;
    app_log_write("INFO", "PS5Torrent v2.0.4 session started");
}

void ui_print_banner(void)
{
    ui_log("");
    ui_log("  ╔══════════════════════════════════════════╗");
    ui_log("  ║         PS5Torrent v2.0.4                ║");
    ui_log("  ║    BitTorrent Client for PlayStation 5    ║");
    ui_log("  ║         Homebrew ELF Payload              ║");
    ui_log("  ╚══════════════════════════════════════════╝");
    ui_log("");
    ui_log("  Built with ps5-payload-sdk");
    ui_log("");
}

void ui_print_torrent_info(const torrent_t *torrent)
{
    if (!torrent) return;

    char hash_str[41];
    torrent_info_hash_str(torrent, hash_str);

    ui_log("=== Torrent Information ===");
    ui_log("  Name:        %s", torrent->name ? torrent->name : "N/A");
    ui_log("  Info Hash:   %s", hash_str);
    ui_log("  Size:        %lld bytes (%.2f MB)",
           (long long)torrent->total_size,
           (double)torrent->total_size / (1024.0 * 1024.0));
    ui_log("  Pieces:      %zu x %lld bytes",
           torrent->num_pieces, (long long)torrent->piece_length);
    ui_log("  Files:       %s (%s)",
           torrent->is_multi_file ? "Multi-file" : "Single file",
           torrent->is_multi_file ? "see file list below" :
           (torrent->file_name ? torrent->file_name : "unnamed"));
    ui_log("  Tracker:     %s", torrent->announce ? torrent->announce : "N/A");

    if (torrent->announce_list_count > 1) {
        ui_log("  Trackers:    %zu total", torrent->announce_list_count);
    }

    if (torrent->is_multi_file && torrent->files) {
        ui_log("  Files:");
        for (size_t i = 0; i < torrent->num_files && i < 10; i++) {
            ui_log("    [%zu] %s (%lld bytes)",
                   i,
                   torrent->files[i].path ? torrent->files[i].path : "?",
                   (long long)torrent->files[i].length);
        }
        if (torrent->num_files > 10) {
            ui_log("    ... and %zu more files", torrent->num_files - 10);
        }
    }
    ui_log("==========================");
}

void ui_print_progress(const torrent_t *torrent, const piece_mgr_t *pm,
                       int active_peers, int64_t downloaded,
                       int64_t uploaded)
{
    if (!pm) return;

    float progress = piece_mgr_progress(pm);
    int pct = (int)(progress * 100.0f);

    /* Build a simple progress bar */
    int bar_width = 40;
    int filled = (int)(progress * (float)bar_width);

    char bar[41];
    for (int i = 0; i < bar_width; i++) {
        bar[i] = (i < filled) ? '#' : (i == filled ? '>' : '.');
    }
    bar[bar_width] = '\0';

    ui_log("\r[%s] %d%% | %zu/%zu pieces | %d peers | "
           "DL: %lld KB | UL: %lld KB",
           bar, pct, pm->num_complete, pm->num_pieces,
           active_peers,
           (long long)(downloaded / 1024),
           (long long)(uploaded / 1024));
}

void ui_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    va_list copy;
    va_copy(copy, args);
    char message[1024];
    vsnprintf(message, sizeof(message), fmt, copy);
    va_end(copy);
    app_log_write("INFO", message);

    if (use_klog) {
        klog_printf("[PS5T] ");
        /* klog_printf supports format strings */
        /* We need to use a temporary buffer for the format */
        char buf[512];
        vsnprintf(buf, sizeof(buf), fmt, args);
        klog_puts(buf);
    } else {
        vprintf(fmt, args);
        printf("\n");
    }

    va_end(args);
}

void ui_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    app_log_write("ERROR", buf);
    if (use_klog) {
        klog_printf("[PS5T] ERROR: %s\n", buf);
    } else {
        fprintf(stderr, "ERROR: %s\n", buf);
    }
}

void ui_success(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    app_log_write("SUCCESS", buf);
    if (use_klog) {
        klog_printf("[PS5T] SUCCESS: %s\n", buf);
    } else {
        printf("SUCCESS: %s\n", buf);
    }
}

void ui_notify(const char *fmt, ...)
{
    notification_request_t request = {0};
    va_list args;
    va_start(args, fmt);
    vsnprintf(request.message, sizeof(request.message), fmt, args);
    va_end(args);

    int result = sceKernelSendNotificationRequest(0, &request,
                                                   sizeof(request), 0);
    if (result != 0)
        ui_error("Could not display PS5 notification (0x%x)", result);
}
