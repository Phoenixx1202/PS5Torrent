/**
 * PS5Torrent v2.0.4 - Web Interface + Multi-Torrent Engine
 *
 * BitTorrent Client for PlayStation 5 with embedded web UI.
 * Access the web interface at http://PS5_IP:12389/
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
#include <sys/syscall.h>
#include <signal.h>
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
#include "app_log.h"
#include "http_server.h"
#include "torrent_mgr.h"
#include "storage_paths.h"
#include "ps5_jailbreak.h"
#include "web_content.h"
#include "console_files.h"
#include "ps5_tile.h"

extern int sceSystemServiceParamGetInt(int param, int *value);
static const char *system_language = "en-US";

static const char *localized(const char *pt, const char *en, const char *es)
{
    if (!strncmp(system_language, "pt", 2)) return pt;
    if (!strncmp(system_language, "es", 2)) return es;
    return en;
}

static void detect_system_language(void)
{
    int language = -1;
    /* SystemService language parameter, shared by PS4 and PS5. */
    if (sceSystemServiceParamGetInt(1, &language) == 0) {
        if (language == 7) system_language = "pt-PT";
        else if (language == 17) system_language = "pt-BR";
        else if (language == 3 || language == 20) system_language = "es-ES";
    }
}

static int local_access_active = 0;

static void json_escape_text(const char *input, char *output, size_t output_size)
{
    static const char hex[] = "0123456789abcdef";
    size_t pos = 0;

    if (!output || output_size == 0) return;
    if (!input) input = "";

    for (const unsigned char *p = (const unsigned char *)input;
         *p && pos + 1 < output_size; p++) {
        unsigned char c = *p;
        if (c == '\"' || c == '\\') {
            if (pos + 2 >= output_size) break;
            output[pos++] = '\\';
            output[pos++] = (char)c;
        } else if (c < 0x20) {
            if (pos + 6 >= output_size) break;
            output[pos++] = '\\';
            output[pos++] = 'u';
            output[pos++] = '0';
            output[pos++] = '0';
            output[pos++] = hex[c >> 4];
            output[pos++] = hex[c & 15];
        } else {
            output[pos++] = (char)c;
        }
    }
    output[pos] = '\0';
}

static int build_save_path(const char *base_path,
                           char *save_path, size_t save_path_size)
{
    size_t base_len;
    int n;

    if (!base_path || base_path[0] != '/' || !save_path ||
        save_path_size == 0)
        return -1;

    /* Keep a custom destination absolute and canonical enough that it cannot
     * escape into an unexpected directory through dot path components. */
    for (const char *p = base_path; *p; p++) {
        if ((unsigned char)*p < 0x20 ||
            (*p == '.' && (p == base_path || p[-1] == '/') &&
             (p[1] == '/' || p[1] == '\0' ||
              (p[1] == '.' && (p[2] == '/' || p[2] == '\0')))))
            return -1;
    }

    base_len = strlen(base_path);
    while (base_len > 1 && base_path[base_len - 1] == '/')
        base_len--;

    n = snprintf(save_path, save_path_size, "%.*s%storrents/",
                 (int)base_len, base_path, base_len == 1 ? "" : "/");
    return n >= 0 && (size_t)n < save_path_size ? 0 : -1;
}

static http_response_t *torrent_start_error_response(int index,
                                                     int remove_torrent)
{
    managed_torrent_t *mt = torrent_mgr_get(index);
    const char *message = mt && mt->error_msg[0]
                        ? mt->error_msg : "Não foi possível iniciar o torrent";
    int status = mt && mt->error_type == TERR_WRITE_FAILED ? 403 : 400;
    ui_error("Torrent start failed: id=%d, error_code=%d, HTTP_status=%d, message=%s",
             index, mt ? (int)mt->error_type : -1, status, message);
    char detailed[512];
    char escaped[512];
    char json[640];

    if (status == 403 && !local_access_active) {
        snprintf(detailed, sizeof(detailed),
                 "%s. O payload não conseguiu obter acesso local; reinicie-o pelo loader",
                 message);
        message = detailed;
    }

    json_escape_text(message, escaped, sizeof(escaped));
    int n = snprintf(json, sizeof(json),
                     "{\"success\":false,\"error\":\"%s\"}", escaped);

    if (remove_torrent)
        torrent_mgr_remove(index);

    return http_response_new(status, "application/json", json, (size_t)n);
}

/**
 * Web interface handler: serves the main HTML page.
 */
static http_response_t *handle_web_index(const http_request_t *req)
{
    return http_response_new(200, "text/html; charset=utf-8",
                             web_index_html, strlen(web_index_html));
}

static http_response_t *handle_api_logs(const http_request_t *req)
{
    if (req->method != HTTP_GET)
        return http_response_new(405, "text/plain", "GET required", 12);
    size_t length = 0;
    char *text = app_log_read(&length);
    if (!text) return http_response_new(500, "text/plain", "Log unavailable", 15);
    http_response_t *response = http_response_new(200, "text/plain; charset=utf-8", text, length);
    free(text);
    return response;
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

    char save_path[512];
    if (build_save_path(path, save_path, sizeof(save_path)) < 0) {
        static const char body[] =
            "{\"success\":false,\"error\":\"Caminho inválido\"}";
        return http_response_new(400, "application/json",
                                 body, sizeof(body) - 1);
    }

    int idx = torrent_mgr_add_magnet(magnet, save_path);
    if (idx < 0) {
        return http_response_new(400, "application/json",
            "{\"success\":false,\"error\":\"Invalid magnet link or "
            "too many torrents\"}", 60);
    }

    /* Start download automatically */
    if (torrent_mgr_start(idx) < 0)
        return torrent_start_error_response(idx, 1);

    managed_torrent_t *mt = torrent_mgr_get(idx);
    char resp_json[512];
    char escaped_name[384];
    json_escape_text(mt ? mt->name : "Unknown",
                     escaped_name, sizeof(escaped_name));
    int n = snprintf(resp_json, sizeof(resp_json),
        "{\"success\":true,\"id\":%d,\"name\":\"%s\"}",
        idx, escaped_name);

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
        static const char error[] =
            "{\"success\":false,\"error\":\"Use multipart upload instead\"}";
        return http_response_new(400, "application/json", error, sizeof(error) - 1);
    }

    http_get_multipart_field(req->body, req->body_len, "path",
                             path, sizeof(path));

    if (!path[0])
        strncpy(path, storage_paths_get_default(), sizeof(path) - 1);

    if (!file_data || file_len == 0) {
        return http_response_new(400, "application/json",
            "{\"success\":false,\"error\":\"Empty file\"}", 41);
    }

    char save_path[512];
    if (build_save_path(path, save_path, sizeof(save_path)) < 0) {
        static const char body[] =
            "{\"success\":false,\"error\":\"Caminho inválido\"}";
        return http_response_new(400, "application/json",
                                 body, sizeof(body) - 1);
    }

    ui_log("Parsing uploaded torrent: bytes=%zu, destination=%s", file_len, save_path);
    int idx = torrent_mgr_add_raw((const unsigned char *)file_data, file_len, save_path);
    ui_log("Torrent upload parsed: bytes=%zu, result=%d", file_len, idx);
    if (idx < 0) {
        return http_response_new(400, "application/json",
            "{\"success\":false,\"error\":\"Invalid torrent file\"}", 50);
    }

    /* Start download automatically */
    if (torrent_mgr_start(idx) < 0)
        return torrent_start_error_response(idx, 1);

    managed_torrent_t *mt = torrent_mgr_get(idx);
    char resp_json[512];
    char escaped_name[384];
    json_escape_text(mt ? mt->name : "Unknown",
                     escaped_name, sizeof(escaped_name));
    int n = snprintf(resp_json, sizeof(resp_json),
        "{\"success\":true,\"id\":%d,\"name\":\"%s\"}",
        idx, escaped_name);

    return http_response_new(200, "application/json", resp_json, (size_t)n);
}

/**
 * API: Torrent action (start/stop/remove).
 */
static http_response_t *handle_api_local(const http_request_t *req)
{
    char source[1024] = {0}, destination[512] = {0}, save_path[512];
    if (req->method != HTTP_POST || !req->body)
        return http_response_new(400, "application/json", "{}", 2);
    http_get_param(req->body, "source", source, sizeof(source));
    http_get_param(req->body, "path", destination, sizeof(destination));
    if (!destination[0]) snprintf(destination, sizeof(destination), "%s", storage_paths_get_default());
    size_t size = 0;
    ui_log("Reading local torrent: source=%s, destination=%s", source, destination);
    unsigned char *data = console_torrent_read(source, &size);
    if (!data || build_save_path(destination, save_path, sizeof(save_path)) < 0) {
        free(data);
        static const char error[] = "{\"error\":\"Cannot read torrent or destination (maximum 1 MB)\"}";
        return http_response_new(400, "application/json", error, sizeof(error) - 1);
    }
    int index = torrent_mgr_add_raw(data, size, save_path);
    ui_log("Local torrent parsed: source=%s, bytes=%zu, result=%d", source, size, index);
    free(data);
    if (index < 0) {
        static const char error[] = "{\"error\":\"Invalid torrent or torrent limit reached\"}";
        return http_response_new(400, "application/json", error, sizeof(error) - 1);
    }
    if (torrent_mgr_start(index) < 0) return torrent_start_error_response(index, 1);
    char escaped[1536], json[1664];
    managed_torrent_t *mt = torrent_mgr_get(index);
    json_escape_text(mt ? mt->name : "torrent", escaped, sizeof(escaped));
    int n = snprintf(json, sizeof(json), "{\"success\":true,\"id\":%d,\"name\":\"%s\"}", index, escaped);
    return http_response_new(200, "application/json", json, (size_t)n);
}

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
        if (strcmp(action, "start") == 0 && id >= 0 && torrent_mgr_get(id))
            return torrent_start_error_response(id, 0);
        static const char body[] =
            "{\"success\":false,\"error\":\"Ação inválida\"}";
        return http_response_new(400, "application/json",
                                 body, sizeof(body) - 1);
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
        int writable = exists && storage_path_is_writable(paths[i].path);
        pos += snprintf(json + pos, 4096 - pos,
            "{\"path\":\"%s\",\"label\":\"%s\",\"icon\":\"%s\","
            "\"exists\":%d,\"writable\":%d,\"removable\":%d,\"default\":%d}",
            paths[i].path, paths[i].label, paths[i].icon,
            exists, writable, paths[i].is_removable, paths[i].is_default);
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
        "{\"status\":\"running\",\"version\":\"2.0.4\",\"jailbroken\":%s,"
        "\"torrents\":%d,\"capacity\":%d,\"language\":\"%s\"}",
        local_access_active ? "true" : "false",
        torrent_mgr_count(), torrent_mgr_capacity(), system_language);
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
 * The payload loader does not guarantee a valid envp third argument, so the
 * entry point deliberately uses the portable no-argument form.
 */
int main(void)
{
    /* Match Spectrum: a closed HTTP/peer/log connection must not kill us. */
    signal(SIGPIPE, SIG_IGN);
    /* etaHEN matches the main thread name to the payload filename. */
    syscall(SYS_thr_set_name, -1, "PS5Torrent.elf");
    /* Initialize subsystems */
    ui_init();
    /* Try before privilege setup as well, so early startup failures leave traces
     * whenever the loader already grants access to the application folder. */
    app_log_open("/data/PS5Torrent");

    ui_print_banner();

    detect_system_language();

    /* Initialize networking first */
    if (net_init() < 0) {
        ui_error("Failed to initialize networking");
        ui_notify("%s", localized("PS5Torrent não iniciou: falha ao preparar a rede.",
                  "PS5Torrent could not start: network initialization failed.",
                  "PS5Torrent no pudo iniciar: error al preparar la red."));
        return 1;
    }

    /* Configure our own credentials and filesystem root, as in Spectrum. */
    if (ps5_request_jailbreak() == 0) {
        local_access_active = 1;
        ui_log("Local SDK filesystem access ready");
    } else {
        ui_error("Local SDK privilege setup incomplete; using available access");
    }

    if (app_log_open("/data/PS5Torrent") < 0)
        ui_error("Persistent logging unavailable; using the in-memory log");
    else ui_log("Persistent log ready: /data/PS5Torrent/PS5Torrent.log");

    /* Initialize HTTP server */
    ui_log("Initializing web interface on port %d...", HTTP_PORT);

    if (http_server_init(HTTP_PORT) < 0) {
        ui_error("Failed to start HTTP server on port %d", HTTP_PORT);
        ui_error("Make sure port %d is not in use.", HTTP_PORT);
        ui_notify(localized("PS5Torrent não iniciou: a porta %d já está em uso.",
                  "PS5Torrent could not start: port %d is in use.",
                  "PS5Torrent no pudo iniciar: el puerto %d está ocupado."),
                  HTTP_PORT);
        net_cleanup();
        return 1;
    }

    /* Register specific API prefixes before the catch-all web route. */
    http_server_register("/api/torrents/add", handle_api_add);
    http_server_register("/api/torrents/upload", handle_api_upload);
    http_server_register("/api/torrents/local", handle_api_local);
    http_server_register("/api/torrents/", handle_api_action);
    http_server_register("/api/torrents", handle_api_torrents);
    http_server_register("/api/paths", handle_api_paths);
    http_server_register("/api/status", handle_api_status);
    http_server_register("/api/logs", handle_api_logs);
    http_server_register("/api/files/mkdir", console_files_mkdir);
    http_server_register("/api/files", console_files_list);

    /* Catch-all web UI handler must remain last. */
    http_server_register("/", handle_web_index);

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
        int writable = exists && storage_path_is_writable(paths[i].path);
        ui_log("  %s %s %s %s",
               paths[i].icon,
               paths[i].label,
               paths[i].path,
               writable ? "✅ Writable" :
               (exists ? "⚠️ Read-only" : "❌ Not mounted"));

        /* Pick first writable path */
        if (writable && !default_path[0]) {
            strncpy(default_path, paths[i].path, sizeof(default_path) - 1);
        }
    }

    /* Get local IP for user info */
    char local_ip[64] = "<unknown>";
    get_local_ip(local_ip, sizeof(local_ip));

    ui_log("========================================");
    ui_log("  🎮 PS5Torrent is running!");
    ui_log("  ");
    ui_log("  Open this URL in your browser:");
    ui_log("  http://%s:%d/", local_ip, HTTP_PORT);
    ui_log("  ");
    ui_log("  📥 Use the web UI to add torrents");
    ui_log("  💾 Save to USB/NVMe/internal storage");
    ui_log("========================================");
    ui_log("");
    ui_log("Status: HTTP server active | Torrent engine ready");

    /* Install the media tile and stay active until the user opens it. */
    int tile_result = ps5_tile_install();
    if (tile_result < 0) {
        ui_error("Media tile installation failed; web service remains available");
        ui_notify("%s", localized("Não foi possível confirmar a instalação do PS5Torrent em Mídias. Consulte o log.",
                  "Could not confirm PS5Torrent installation in Media. Check the log.",
                  "No se pudo confirmar la instalación de PS5Torrent en Medios. Consulta el registro."));
    } else if (tile_result == 1) {
        ui_notify("%s", localized("Instalação do PS5Torrent aceita. Aguardando registro em Mídias.",
                  "PS5Torrent installation accepted. Waiting for Media registration.",
                  "Instalación de PS5Torrent aceptada. Esperando el registro en Medios."));
    } else {
        ui_notify("%s", localized("PS5Torrent v2.0.4 pronto! Abra o aplicativo na aba Mídias.",
                  "PS5Torrent v2.0.4 is ready! Open the app in the Media tab.",
                  "¡PS5Torrent v2.0.4 está listo! Abre la aplicación en la pestaña Medios."));
    }

    if (default_path[0]) {
        ui_log("💡 Storage detected at: %s", default_path);
    }

    /* Main event loop */
    uint64_t last_status = 0;
    int running = 1;

    while (running) {
        uint64_t now = get_time_ms();
        static uint64_t last_tile_poll = 0;
        if (tile_result == 1 && now - last_tile_poll >= 1000) {
            last_tile_poll = now;
            tile_result = ps5_tile_poll();
            if (tile_result == 0)
                ui_notify("%s", localized("PS5Torrent v2.0.4 pronto! Abra o aplicativo na aba Mídias.",
                          "PS5Torrent v2.0.4 is ready! Open the app in the Media tab.",
                          "¡PS5Torrent v2.0.4 está listo! Abre la aplicación en la pestaña Medios."));
        }


        /* Poll HTTP server (5ms timeout - quick check) */
        int ret = http_server_poll(5);
        if (ret < 0) {
            ui_error("HTTP listener failed: errno=%d (%s)", errno, strerror(errno));
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
    app_log_close();
    return 0;
}
