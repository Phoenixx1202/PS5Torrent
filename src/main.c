/**
 * PS5Torrent v2.0 - Web Interface + Multi-Torrent Engine
 *
 * BitTorrent Client for PlayStation 5 with embedded web UI.
 * Access the web interface at http://PS5_IP:8080/
 *
 * Features:
 *   - Add torrents via magnet links or .torrent file upload
 *   - Choose from known PS5 storage paths (USB, NVMe, internal, etc.)
 *   - Multi-torrent management with simultaneous downloads
 *   - Real-time progress via web UI
 *   - klog console logging
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "sha1.h"
#include "bencode.h"
#include "torrent.h"
#include "net_utils.h"
#include "tracker.h"
#include "peer_wire.h"
#include "piece_mgr.h"
#include "file_io.h"
#include "ui.h"
#include "http_server.h"
#include "torrent_mgr.h"
#include "storage_paths.h"
#include "web_content.h"

/**
 * Web interface handler: serves the main HTML page.
 */
static http_response_t *handle_web_index(const http_request_t *req)
{
    return http_response_new(200, "text/html; charset=utf-8",
                             web_index_html, strlen(web_index_html));
}

/**
 * API: Get torrent status list.
 */
static http_response_t *handle_api_torrents(const http_request_t *req)
{
    char *json = malloc(16384);
    if (!json) return http_response_new(500, "application/json", "{}", 2);

    torrent_mgr_status_json(json, 16384);
    http_response_t *resp = http_response_new(200, "application/json", json, strlen(json));
    free(json);
    return resp;
}

/**
 * API: Add torrent via magnet link (POST form data).
 */
static http_response_t *handle_api_add(const http_request_t *req)
{
    if (req->method != HTTP_POST || !req->body) {
        return http_response_new(400, "application/json",
                                 "{\"success\":false,\"error\":\"POST required\"}", 43);
    }

    char magnet[2048] = {0};
    char path[512] = {0};

    http_get_param(req->body, "magnet", magnet, sizeof(magnet));
    http_get_param(req->body, "path", path, sizeof(path));

    if (!magnet[0]) {
        return http_response_new(400, "application/json",
                                 "{\"success\":false,\"error\":\"Magnet link required\"}", 50);
    }

    if (!path[0])
        strncpy(path, storage_paths_get_default(), sizeof(path) - 1);

    /* Normalize path - ensure it ends with / and has /torrents subdir */
    char save_path[512];
    int n = snprintf(save_path, sizeof(save_path), "%storrents/", path);
    if (n < 0 || (size_t)n >= sizeof(save_path)) {
        return http_response_new(500, "application/json",
                                 "{\"success\":false,\"error\":\"Path too long\"}", 44);
    }

    int idx = torrent_mgr_add_magnet(magnet, save_path);
    if (idx < 0) {
        return http_response_new(400, "application/json",
            "{\"success\":false,\"error\":\"Invalid magnet link or "
            "too many torrents\"}", 60);
    }

    /* Start download automatically */
    torrent_mgr_start(idx);

    managed_torrent_t *mt = torrent_mgr_get(idx);
    char resp_json[512];
    n = snprintf(resp_json, sizeof(resp_json),
        "{\"success\":true,\"id\":%d,\"name\":\"%s\"}",
        idx, mt ? mt->name : "Unknown");

    return http_response_new(200, "application/json", resp_json, (size_t)n);
}

/**
 * API: Upload .torrent file (POST multipart).
 */
static http_response_t *handle_api_upload(const http_request_t *req)
{
    if (req->method != HTTP_POST || !req->body || req->body_len == 0) {
        return http_response_new(400, "application/json",
                                 "{\"success\":false,\"error\":\"File required\"}", 44);
    }

    const char *file_data = NULL;
    size_t file_len = 0;
    char filename[256] = {0};
    char path[512] = {0};

    /* Try multipart form data */
    if (!http_get_upload(req->body, req->body_len, "torrent",
                         &file_data, &file_len, filename, sizeof(filename))) {
        /* Try URL-encoded form */
        char data_b64[HTTP_MAX_BODY] = {0};
        http_get_param(req->body, "torrent_data", data_b64, sizeof(data_b64));
        http_get_param(req->body, "path", path, sizeof(path));

        if (!data_b64[0]) {
            return http_response_new(400, "application/json",
                "{\"success\":false,\"error\":\"No torrent file found\"}", 51);
        }

        /* Base64 decode not implemented in this simple version */
        return http_response_new(400, "application/json",
            "{\"success\":false,\"error\":\"Use multipart upload instead\"}", 56);
    }

    /* Get path from multipart */
    /* Parse path from the multipart body - look for name="path" */
    const char *path_field = strstr(req->body, "name=\"path\"");
    if (path_field) {
        const char *val = strstr(path_field, "\r\n\r\n");
        if (val) {
            val += 4;
            const char *val_end = strstr(val, "\r\n");
            size_t vl = val_end ? (size_t)(val_end - val) : strlen(val);
            if (vl >= sizeof(path)) vl = sizeof(path) - 1;
            memcpy(path, val, vl);
            path[vl] = '\0';
        }
    }

    if (!path[0])
        strncpy(path, storage_paths_get_default(), sizeof(path) - 1);

    if (!file_data || file_len == 0) {
        return http_response_new(400, "application/json",
            "{\"success\":false,\"error\":\"Empty file\"}", 41);
    }

    char save_path[512];
    int n = snprintf(save_path, sizeof(save_path), "%storrents/", path);
    if (n < 0 || (size_t)n >= sizeof(save_path)) {
        return http_response_new(500, "application/json",
                                 "{\"success\":false,\"error\":\"Path too long\"}", 44);
    }

    int idx = torrent_mgr_add_raw((const unsigned char *)file_data, file_len, save_path);
    if (idx < 0) {
        return http_response_new(400, "application/json",
            "{\"success\":false,\"error\":\"Invalid torrent file\"}", 50);
    }

    /* Start download automatically */
    torrent_mgr_start(idx);

    managed_torrent_t *mt = torrent_mgr_get(idx);
    char resp_json[512];
    n = snprintf(resp_json, sizeof(resp_json),
        "{\"success\":true,\"id\":%d,\"name\":\"%s\"}",
        idx, mt ? mt->name : "Unknown");

    return http_response_new(200, "application/json", resp_json, (size_t)n);
}

/**
 * API: Torrent action (start/stop/remove).
 */
static http_response_t *handle_api_action(const http_request_t *req)
{
    if (req->method != HTTP_POST) {
        return http_response_new(400, "application/json",
                                 "{\"success\":false,\"error\":\"POST required\"}", 43);
    }

    /* Parse path: /api/torrents/<id>/<action> */
    int id = -1;
    char action[16] = {0};

    /* Extract ID and action from path */
    const char *p = req->path;
    /* Skip /api/torrents/ (14 characters) */
    p += 14;

    /* Read digits for ID */
    while (*p >= '0' && *p <= '9') {
        if (id < 0) id = 0;
        id = id * 10 + (*p - '0');
        p++;
    }

    if (id < 0 || *p != '/') {
        return http_response_new(400, "application/json",
                                 "{\"success\":false,\"error\":\"Invalid path\"}", 43);
    }

    p++; // skip '/'
    strncpy(action, p, sizeof(action) - 1);

    int result = -1;
    if (strcmp(action, "start") == 0)
        result = torrent_mgr_start(id);
    else if (strcmp(action, "stop") == 0) {
        torrent_mgr_stop(id);
        result = 0;
    } else if (strcmp(action, "remove") == 0) {
        torrent_mgr_remove(id);
        result = 0;
    }

    if (result == 0) {
        char resp[128];
        int n = snprintf(resp, sizeof(resp),
                         "{\"success\":true,\"action\":\"%s\",\"id\":%d}", action, id);
        return http_response_new(200, "application/json", resp, (size_t)n);
    } else {
        return http_response_new(400, "application/json",
            "{\"success\":false,\"error\":\"Action failed\"}", 44);
    }
}

/**
 * API: Get available paths.
 */
static http_response_t *handle_api_paths(const http_request_t *req)
{
    storage_path_t paths[MAX_STORAGE_PATHS];
    size_t count = 0;
    storage_paths_get(paths, &count);

    char *json = malloc(4096);
    if (!json) return http_response_new(500, "application/json", "{}", 2);

    size_t pos = 0;
    pos += snprintf(json + pos, 4096 - pos, "{\"paths\":[");

    for (size_t i = 0; i < count; i++) {
        if (i > 0) pos += snprintf(json + pos, 4096 - pos, ",");
        int exists = storage_path_exists(paths[i].path);
        pos += snprintf(json + pos, 4096 - pos,
            "{\"path\":\"%s\",\"label\":\"%s\",\"icon\":\"%s\","
            "\"exists\":%d,\"removable\":%d,\"default\":%d}",
            paths[i].path, paths[i].label, paths[i].icon,
            exists, paths[i].is_removable, paths[i].is_default);
    }

    pos += snprintf(json + pos, 4096 - pos, "]}");

    http_response_t *resp = http_response_new(200, "application/json", json, pos);
    free(json);
    return resp;
}

/**
 * API: Get server status.
 */
static http_response_t *handle_api_status(const http_request_t *req)
{
    char resp[512];
    int n = snprintf(resp, sizeof(resp),
        "{\"status\":\"running\",\"version\":\"2.0\","
        "\"torrents\":%d,\"capacity\":%d}",
        torrent_mgr_count(), torrent_mgr_capacity());
    return http_response_new(200, "application/json", resp, (size_t)n);
}

/**
 * Calculate monotonic time in milliseconds.
 */
static uint64_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

/**
 * Get a network interface's IPv4 address using POSIX socket API.
 * Works on PS5/FreeBSD without shell commands.
 * Returns 0 if IP found, -1 if not.
 */
static int get_local_ip(char *buf, size_t bufsz)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        strncpy(buf, "192.168.x.x", bufsz - 1);
        return -1;
    }

    struct ifconf ifc;
    struct ifreq ifr[16];

    ifc.ifc_len = sizeof(ifr);
    ifc.ifc_req = ifr;

    if (ioctl(fd, SIOCGIFCONF, &ifc) < 0) {
        close(fd);
        strncpy(buf, "192.168.x.x", bufsz - 1);
        return -1;
    }

    int found = -1;
    int num_if = ifc.ifc_len / sizeof(struct ifreq);

    for (int i = 0; i < num_if; i++) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&ifr[i].ifr_addr;
        uint32_t ip = ntohl(sin->sin_addr.s_addr);

        /* Skip loopback (127.x.x.x) */
        if ((ip >> 24) == 127) continue;
        /* Skip link-local (169.254.x.x) */
        if ((ip >> 16) == 0xA9FE) continue;
        /* Skip zeros */
        if (ip == 0) continue;

        /* Found a valid IP */
        snprintf(buf, bufsz, "%d.%d.%d.%d",
                 (int)(ip >> 24) & 0xFF,
                 (int)(ip >> 16) & 0xFF,
                 (int)(ip >> 8) & 0xFF,
                 (int)(ip & 0xFF));
        found = 0;
        break;
    }

    close(fd);

    if (found < 0) {
        strncpy(buf, "192.168.x.x", bufsz - 1);
    }

    return found;
}

/**
 * Entry point called by ps5-payload-sdk CRT.
 * Supports two modes:
 *   1. PKG mode: Launched from PS5 dashboard (no args) → auto-starts web UI
 *   2. CLI mode: Launched from ELF loader with args → original behavior
 */
int main(int argc, char *argv[], char *envp[])
{
    int is_pkg_mode = 0;

    /* Detect PKG mode: check environment for PS5 app signatures */
    /* When launched from PS5 dashboard via PKG, argv/argc may vary, */
    /* but envp typically contains APP_LAUNCH_TYPE or similar */
    for (char **env = envp; env && *env; env++) {
        if (strncmp(*env, "APP_LAUNCH_TYPE=", 16) == 0 ||
            strncmp(*env, "SCE_APP_TYPE=", 13) == 0 ||
            strncmp(*env, "PS5TORRENT_AUTO=", 16) == 0 ||
            strncmp(*env, "SYS_LAUNCH_PATH=", 16) == 0) {
            is_pkg_mode = 1;
            break;
        }
    }

    /* Also detect via argv: if program name contains /mnt/ or /data/, it's PKG mode */
    if (!is_pkg_mode && argc >= 1 && argv && argv[0]) {
        if (strstr(argv[0], "/mnt/") || strstr(argv[0], "/data/") ||
            strstr(argv[0], "/sandbox/")) {
            is_pkg_mode = 1;
        }
    }

    /* Last resort: if no args at all, assume PKG */
    if (!is_pkg_mode && argc <= 1) {
        is_pkg_mode = 1;
    }

    /* Initialize subsystems */
    ui_init();

    if (is_pkg_mode) {
        ui_log("🎮 PS5Torrent launched from PS5 dashboard!");
        ui_log("   Auto-starting web interface...");
    }

    ui_print_banner();

    /* Initialize networking first */
    if (net_init() < 0) {
        ui_error("Failed to initialize networking");
        return 1;
    }

    /* Initialize HTTP server */
    ui_log("Initializing web interface on port %d...", HTTP_PORT);

    if (http_server_init(HTTP_PORT) < 0) {
        ui_error("Failed to start HTTP server on port %d", HTTP_PORT);
        ui_error("Make sure port %d is not in use.", HTTP_PORT);
        net_cleanup();
        return 1;
    }

    /* Register web UI handler */
    http_server_register("/", handle_web_index);

    /* Register API handlers */
    http_server_register("/api/torrents/add", handle_api_add);
    http_server_register("/api/torrents/upload", handle_api_upload);
    http_server_register("/api/torrents/", handle_api_action);
    http_server_register("/api/torrents", handle_api_torrents);
    http_server_register("/api/paths", handle_api_paths);
    http_server_register("/api/status", handle_api_status);

    /* Initialize torrent manager */
    torrent_mgr_init();

    /* Show available storage paths */
    storage_path_t paths[MAX_STORAGE_PATHS];
    size_t num_paths = 0;
    storage_paths_get(paths, &num_paths);

    ui_log("Available storage paths:");

    char default_path[256] = {0};
    for (size_t i = 0; i < num_paths; i++) {
        int exists = storage_path_exists(paths[i].path);
        ui_log("  %s %s %s %s",
               paths[i].icon,
               paths[i].label,
               paths[i].path,
               exists ? "✅ Available" : "❌ Not mounted");

        /* Pick first available path */
        if (exists && !default_path[0]) {
            strncpy(default_path, paths[i].path, sizeof(default_path) - 1);
        }
    }

    /* Get local IP for user info */
    char local_ip[64] = "<unknown>";
    get_local_ip(local_ip, sizeof(local_ip));

    ui_log("========================================");
    ui_log("  🎮 PS5Torrent is running!");
    ui_log("  ");
    if (is_pkg_mode) {
        ui_log("  📱 Open this URL in your browser:");
        ui_log("  🌐 http://%s:%d/", local_ip, HTTP_PORT);
    } else {
        ui_log("  Access the web interface at:");
        ui_log("  http://<PS5_IP>:%d/", HTTP_PORT);
    }
    ui_log("  ");
    ui_log("  📥 Use the web UI to add torrents");
    ui_log("  💾 Save to USB/NVMe/internal storage");
    ui_log("========================================");
    ui_log("");
    ui_log("Status: HTTP server active | Torrent engine ready");

    /* If in PKG mode and we found a default path, show tip */
    if (is_pkg_mode && default_path[0]) {
        ui_log("💡 Storage detected at: %s", default_path);
    }

    /* Main event loop */
    uint64_t last_status = 0;
    int running = 1;

    while (running) {
        uint64_t now = get_time_ms();

        /* Poll HTTP server (5ms timeout - quick check) */
        int ret = http_server_poll(5);
        if (ret < 0) {
            ui_error("HTTP server error, shutting down...");
            break;
        }

        /* Tick torrent manager */
        int active = torrent_mgr_tick();

        /* Print status every 5 seconds */
        if (now - last_status > 5000) {
            last_status = now;
            if (active > 0) {
                ui_log("Active torrents: %d | Total: %d/%d",
                       active, torrent_mgr_count(), torrent_mgr_capacity());
            }
        }

        /* Small delay to prevent busy-waiting */
        usleep(10000); // 10ms
    }

    /* Cleanup */
    ui_log("Shutting down...");
    torrent_mgr_shutdown();
    http_server_shutdown();
    net_cleanup();

    ui_log("PS5Torrent terminated.");
    return 0;
}
