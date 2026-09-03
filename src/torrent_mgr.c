#include "torrent_mgr.h"
#include "net_utils.h"
#include "sha1.h"
#include "http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>

/**
 * Managed torrents array.
 */
static managed_torrent_t torrents[MAX_TORRENTS];
static int num_torrents = 0;

static int ensure_directory_tree(const char *path)
{
    char current[512];
    size_t len;

    if (!path || path[0] != '/') {
        errno = EINVAL;
        return -1;
    }

    len = strlen(path);
    if (len == 0 || len >= sizeof(current)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(current, path, len + 1);
    while (len > 1 && current[len - 1] == '/')
        current[--len] = '\0';

    for (char *cursor = current + 1;; cursor++) {
        if (*cursor != '/' && *cursor != '\0') continue;

        char separator = *cursor;
        struct stat st;
        *cursor = '\0';

        if (mkdir(current, 0755) < 0 && errno != EEXIST) {
            *cursor = separator;
            return -1;
        }
        if (stat(current, &st) < 0) {
            *cursor = separator;
            return -1;
        }
        if (!S_ISDIR(st.st_mode)) {
            errno = ENOTDIR;
            *cursor = separator;
            return -1;
        }

        *cursor = separator;
        if (separator == '\0') break;
    }

    return 0;
}

/**
 * Get current time in seconds (monotonic-ish).
 */
static uint64_t get_time_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec;
}

void torrent_mgr_init(void)
{
    memset(torrents, 0, sizeof(torrents));
    num_torrents = 0;
}

int torrent_mgr_add(torrent_t *torrent, const char *save_path,
                    int is_magnet, const char *magnet_uri)
{
    if (!torrent) return -1;
    if (num_torrents >= MAX_TORRENTS) return -1;

    int idx = num_torrents;
    managed_torrent_t *mt = &torrents[idx];
    memset(mt, 0, sizeof(managed_torrent_t));

    /* Set ID from info hash */
    char hash_hex[41];
    torrent_info_hash_str(torrent, hash_hex);
    strncpy(mt->id, hash_hex, sizeof(mt->id) - 1);

    /* Set name */
    if (torrent->name)
        strncpy(mt->name, torrent->name, sizeof(mt->name) - 1);
    else
        snprintf(mt->name, sizeof(mt->name), "Torrent_%s", hash_hex);

    strncpy(mt->save_path, save_path, sizeof(mt->save_path) - 1);
    memcpy(mt->info_hash, torrent->info_hash, 20);
    mt->torrent = torrent;
    mt->file_writer.fd = -1;
    mt->total_size = torrent->total_size;
    mt->is_magnet = is_magnet;
    if (magnet_uri)
        strncpy(mt->magnet_uri, magnet_uri, sizeof(mt->magnet_uri) - 1);

    /* Initialize peer connections */
    for (int i = 0; i < MAX_PEERS_PER_TORRENT; i++)
        mt->peers[i].sock = -1;

    /* Generate peer ID */
    tracker_generate_peer_id(mt->peer_id);

    mt->state = TORRENT_STOPPED;
    num_torrents++;

    return idx;
}

int torrent_mgr_add_raw(const unsigned char *data, size_t data_len,
                        const char *save_path)
{
    torrent_t *t = torrent_parse(data, data_len);
    if (!t) return -1;
    return torrent_mgr_add(t, save_path, 0, NULL);
}

int torrent_mgr_add_magnet(const char *magnet_uri, const char *save_path)
{
    /* Parse magnet URI: magnet:?xt=urn:btih:<infohash>&dn=<name>&tr=<tracker> */
    if (!magnet_uri || strncmp(magnet_uri, "magnet:", 7) != 0)
        return -1;

    /* Extract info hash */
    const char *xt = strstr(magnet_uri, "xt=urn:btih:");
    if (!xt) return -1;
    xt += 12; // skip "xt=urn:btih:"

    char hash_str[41];
    int hash_pos = 0;
    while (*xt && *xt != '&' && *xt != '?' && hash_pos < 40) {
        hash_str[hash_pos++] = *xt++;
    }
    hash_str[hash_pos] = '\0';

    /* Convert hex hash to binary */
    unsigned char info_hash[20];
    for (int i = 0; i < 20; i++) {
        char byte[3] = {hash_str[i*2], hash_str[i*2+1], 0};
        info_hash[i] = (unsigned char)strtol(byte, NULL, 16);
    }

    /* Extract display name */
    char display_name[256] = {0};
    const char *dn = strstr(magnet_uri, "dn=");
    if (dn) {
        dn += 3;
        int dn_pos = 0;
        while (*dn && *dn != '&' && dn_pos < 255) {
            display_name[dn_pos++] = *dn++;
        }
        display_name[dn_pos] = '\0';
        http_url_decode(display_name);
    }

    /* Build a minimal torrent_t from the magnet data */
    torrent_t *t = calloc(1, sizeof(torrent_t));
    if (!t) return -1;

    memcpy(t->info_hash, info_hash, 20);

    if (display_name[0]) {
        t->name = strdup(display_name);
        if (t->name) t->name_len = strlen(t->name);
    } else {
        t->name = strdup(hash_str);
        if (t->name) t->name_len = 40;
    }

    /* Extract tracker URLs */
    const char *tr_ptr = magnet_uri;
    int tracker_count = 0;
    t->announce_list = calloc(8, sizeof(char *));

    while ((tr_ptr = strstr(tr_ptr, "tr=")) != NULL && tracker_count < 8) {
        tr_ptr += 3;
        const char *tr_end = strchr(tr_ptr, '&');
        size_t tr_len = tr_end ? (size_t)(tr_end - tr_ptr) : strlen(tr_ptr);

        char *decoded = malloc(tr_len + 1);
        if (decoded) {
            memcpy(decoded, tr_ptr, tr_len);
            decoded[tr_len] = '\0';
            http_url_decode(decoded);

            t->announce_list[tracker_count++] = decoded;

            /* First tracker is the primary announce */
            if (tracker_count == 1) {
                t->announce = strdup(decoded);
                if (t->announce) t->announce_len = strlen(decoded);
            }
        }

        if (!tr_end) break;
        tr_ptr = tr_end;
    }
    t->announce_list_count = (size_t)tracker_count;

    /* Magnet links don't have piece info - we need to get metadata
     * from peers first. For now, mark as magnet and return.
     * The download will start when we connect to peers and get metadata. */
    t->piece_length = 0;
    t->num_pieces = 0;
    t->total_size = 0;
    t->is_multi_file = 0;

    int idx = torrent_mgr_add(t, save_path, 1, magnet_uri);

    /* If torrent_mgr_add failed, free the torrent we allocated */
    if (idx < 0) {
        torrent_free(t);
        return -1;
    }

    return idx;
}

int torrent_mgr_start(int index)
{
    if (index < 0 || index >= num_torrents) return -1;
    managed_torrent_t *mt = &torrents[index];

    if (mt->state != TORRENT_STOPPED && mt->state != TORRENT_ERROR)
        return -1;
    if (!mt->torrent) return -1;

    if (mt->state == TORRENT_ERROR) {
        piece_mgr_destroy(&mt->piece_mgr);
        file_writer_destroy(&mt->file_writer);
    }
    mt->error_type = TERR_NONE;
    mt->error_msg[0] = '\0';

    /* Create save directory */
    if (ensure_directory_tree(mt->save_path) < 0) {
        int saved_errno = errno;
        mt->state = TORRENT_ERROR;
        mt->error_type = TERR_WRITE_FAILED;
        if (saved_errno == EACCES || saved_errno == EPERM) {
            snprintf(mt->error_msg, sizeof(mt->error_msg),
                     "Sem permissão para criar %s", mt->save_path);
        } else {
            snprintf(mt->error_msg, sizeof(mt->error_msg),
                     "Não foi possível criar %s: %s",
                     mt->save_path, strerror(saved_errno));
        }
        return -1;
    }

    /* For magnet links without metadata, we need to do metadata download first */
    if (mt->is_magnet && mt->torrent->num_pieces == 0) {
        /* For now, mark as error - magnet metadata download
         * requires BEP-9 (extension protocol) */
        mt->state = TORRENT_ERROR;
        mt->error_type = TERR_PARSE_FAILED;
        snprintf(mt->error_msg, sizeof(mt->error_msg),
                 "Magnet links require metadata download (BEP-9) - "
                 "use a .torrent file instead");
        return -1;
    }

    /* Initialize piece manager */
    piece_mgr_init(&mt->piece_mgr,
                   mt->torrent->num_pieces,
                   mt->torrent->piece_length,
                   mt->torrent->total_size,
                   mt->torrent->pieces);

    /* Initialize file writer */
    if (file_writer_init(&mt->file_writer, mt->torrent, mt->save_path) < 0) {
        int saved_errno = errno;
        mt->state = TORRENT_ERROR;
        mt->error_type = TERR_WRITE_FAILED;
        snprintf(mt->error_msg, sizeof(mt->error_msg),
                 "Não foi possível criar o arquivo em %s: %s",
                 mt->save_path, strerror(saved_errno));
        return -1;
    }

    mt->state = TORRENT_DOWNLOADING;
    mt->start_time = get_time_sec();
    mt->last_announce_time = 0;
    mt->last_speed_calc = get_time_sec();
    mt->last_downloaded = mt->downloaded;
    mt->last_uploaded = mt->uploaded;

    return 0;
}

void torrent_mgr_stop(int index)
{
    if (index < 0 || index >= num_torrents) return;
    managed_torrent_t *mt = &torrents[index];

    mt->state = TORRENT_STOPPED;

    /* Close all peer connections */
    for (int i = 0; i < mt->num_peers; i++) {
        if (mt->peers[i].sock >= 0) {
            net_close(mt->peers[i].sock);
            mt->peers[i].sock = -1;
        }
    }
    mt->num_peers = 0;
    mt->active_peers = 0;
    mt->speed_down = 0;
    mt->speed_up = 0;

    file_writer_finish(&mt->file_writer);
}

void torrent_mgr_remove(int index)
{
    if (index < 0 || index >= num_torrents) return;

    torrent_mgr_stop(index);

    managed_torrent_t *mt = &torrents[index];

    piece_mgr_destroy(&mt->piece_mgr);
    file_writer_destroy(&mt->file_writer);
    torrent_free(mt->torrent);
    mt->torrent = NULL;

    /* Shift remaining torrents */
    for (int i = index; i < num_torrents - 1; i++) {
        torrents[i] = torrents[i + 1];
    }
    num_torrents--;
    memset(&torrents[num_torrents], 0, sizeof(managed_torrent_t));
}

managed_torrent_t *torrent_mgr_get(int index)
{
    if (index < 0 || index >= num_torrents) return NULL;
    return &torrents[index];
}

int torrent_mgr_count(void)
{
    return num_torrents;
}

int torrent_mgr_capacity(void)
{
    return MAX_TORRENTS;
}

int torrent_mgr_find(const unsigned char info_hash[20])
{
    for (int i = 0; i < num_torrents; i++) {
        if (memcmp(torrents[i].info_hash, info_hash, 20) == 0)
            return i;
    }
    return -1;
}

const char *torrent_mgr_error_str(torrent_error_t err)
{
    switch (err) {
    case TERR_NONE:          return "No error";
    case TERR_PARSE_FAILED:  return "Parse failed";
    case TERR_TRACKER_FAILED:return "Tracker failed";
    case TERR_NO_PEERS:      return "No peers found";
    case TERR_WRITE_FAILED:  return "Write failed";
    case TERR_HASH_MISMATCH: return "Hash mismatch";
    default:                 return "Unknown error";
    }
}

/**
 * Announce to tracker for a specific torrent.
 */
static int announce_to_tracker(managed_torrent_t *mt)
{
    if (!mt->torrent->announce) return -1;

    tracker_params_t params;
    params.info_hash = mt->torrent->info_hash;
    params.peer_id = mt->peer_id;
    params.port = 6881;
    params.uploaded = (int64_t)mt->uploaded;
    params.downloaded = (int64_t)mt->downloaded;
    params.left = (int64_t)(mt->total_size - mt->downloaded);
    params.compact = 1;

    tracker_response_t *tr = tracker_announce(mt->torrent->announce, &params);
    if (!tr) return -1;

    mt->tracker_announces++;

    if (tr->failure_reason) {
        tracker_response_free(tr);
        return -1;
    }

    mt->tracker_interval = tr->interval;

    /* Connect to new peers */
    if (tr->num_peers > 0 && tr->peers) {
        for (int i = 0; i < tr->num_peers && mt->num_peers < MAX_PEERS_PER_TORRENT; i++) {
            /* Check if already connected */
            int already = 0;
            for (int j = 0; j < mt->num_peers; j++) {
                if (mt->peers[j].sock >= 0 &&
                    mt->peers[j].addr.ip == tr->peers[i].ip &&
                    mt->peers[j].addr.port == tr->peers[i].port) {
                    already = 1;
                    break;
                }
            }
            if (already) continue;

            /* Connect to peer */
            int sock = net_tcp_connect(tr->peers[i].ip,
                                       ntohs(tr->peers[i].port));
            if (sock < 0) continue;

            net_set_timeout(sock, 15);

            uint8_t remote_id[20];
            if (peer_handshake(sock, mt->torrent->info_hash,
                               mt->peer_id, remote_id) < 0) {
                net_close(sock);
                continue;
            }

            /* Setup peer */
            torrent_peer_t *p = &mt->peers[mt->num_peers];
            p->sock = sock;
            p->addr = tr->peers[i];
            memcpy(p->peer_id, remote_id, 20);
            p->choked = 1;
            p->am_interested = 0;
            p->last_request = 0;

            mt->num_peers++;
            mt->active_peers++;

            peer_send_interested(sock);
        }
    }

    tracker_response_free(tr);
    return 0;
}

/**
 * Process one peer for a torrent - try to recv messages and send requests.
 */
static void process_peer(managed_torrent_t *mt, int peer_idx)
{
    torrent_peer_t *p = &mt->peers[peer_idx];
    if (p->sock < 0) return;

    uint8_t piece_buf[256 * 1024];
    uint32_t piece_idx = 0, piece_begin = 0, block_len = 0;

    int ret = peer_recv_message(p->sock, &piece_idx, &piece_begin,
                                piece_buf, &block_len, 0);

    if (ret < 0) {
        /* Peer disconnected */
        net_close(p->sock);
        p->sock = -1;
        mt->active_peers--;
        return;
    }

    if (ret == 0 && block_len > 0) {
        /* Received piece data! */
        int result = piece_mgr_complete(&mt->piece_mgr,
                                        (size_t)piece_idx,
                                        piece_buf, block_len);
        if (result == 0) {
            /* Write to disk */
            file_writer_write_piece(&mt->file_writer,
                                    piece_buf, block_len,
                                    (size_t)piece_idx);
            mt->downloaded += block_len;
            p->downloaded += block_len;
        } else {
            piece_mgr_failed(&mt->piece_mgr, (size_t)piece_idx);
        }
        return;
    }

    /* Non-piece message or nothing to receive - send request if unchoked */
    if (!p->choked && mt->state == TORRENT_DOWNLOADING) {
        int next = piece_mgr_get_next(&mt->piece_mgr, NULL, 0);
        if (next >= 0) {
            int64_t psize = mt->torrent->piece_length;
            if ((size_t)next == mt->torrent->num_pieces - 1) {
                psize = mt->torrent->total_size -
                        (int64_t)next * mt->torrent->piece_length;
            }
            if (peer_send_request(p->sock, (uint32_t)next,
                                  0, (uint32_t)psize) == 0) {
                piece_mgr_request(&mt->piece_mgr, (size_t)next);
                p->last_request = (uint32_t)next;
            }
        }
    }
}

int torrent_mgr_tick(void)
{
    int active = 0;
    uint64_t now = get_time_sec();

    for (int i = 0; i < num_torrents; i++) {
        managed_torrent_t *mt = &torrents[i];
        if (mt->state != TORRENT_DOWNLOADING) continue;

        active++;

        /* Check if complete */
        if (piece_mgr_is_done(&mt->piece_mgr)) {
            file_writer_finish(&mt->file_writer);
            mt->state = TORRENT_DONE;
            mt->progress = 1.0f;
            mt->speed_down = 0;
            mt->speed_up = 0;
            continue;
        }

        /* Update progress */
        mt->progress = piece_mgr_progress(&mt->piece_mgr);

        /* Calculate speed */
        if (now > mt->last_speed_calc) {
            uint64_t elapsed = now - mt->last_speed_calc;
            if (elapsed >= 2) {
                mt->speed_down = (mt->downloaded - mt->last_downloaded) / elapsed;
                mt->speed_up = (mt->uploaded - mt->last_uploaded) / elapsed;
                mt->last_speed_calc = now;
                mt->last_downloaded = mt->downloaded;
                mt->last_uploaded = mt->uploaded;
            }
        }

        /* Re-announce to tracker periodically */
        if (mt->tracker_interval > 0 &&
            now - mt->last_announce_time >= (uint64_t)mt->tracker_interval) {
            announce_to_tracker(mt);
            mt->last_announce_time = now;
        }

        /* Initial announce */
        if (mt->last_announce_time == 0) {
            announce_to_tracker(mt);
            mt->last_announce_time = now;
        }

        /* Process peers with non-blocking reads */
        for (int p = 0; p < mt->num_peers; p++) {
            torrent_peer_t *peer = &mt->peers[p];
            if (peer->sock >= 0) {
                /* Set very short timeout for non-blocking check */
                struct timeval tv = {0, 0};
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(peer->sock, &readfds);

                if (select(peer->sock + 1, &readfds, NULL, NULL, &tv) > 0) {
                    process_peer(mt, p);
                }
            }
        }

        /* If no active peers, try to re-announce */
        if (mt->active_peers == 0 && now - mt->last_announce_time >= 5) {
            announce_to_tracker(mt);
            mt->last_announce_time = now;
        }

        /* Check if stalled */
        if (mt->active_peers == 0 && mt->tracker_announces > 3) {
            mt->state = TORRENT_ERROR;
            mt->speed_down = 0;
            mt->speed_up = 0;
            mt->error_type = TERR_NO_PEERS;
            snprintf(mt->error_msg, sizeof(mt->error_msg),
                     "No peers found after %d tracker announces",
                     mt->tracker_announces);
        }
    }

    return active;
}

typedef struct {
    char   *buf;
    size_t  size;
    size_t  pos;
} json_writer_t;

static void json_writef(json_writer_t *writer, const char *fmt, ...)
{
    if (!writer || writer->pos >= writer->size) return;

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(writer->buf + writer->pos,
                            writer->size - writer->pos, fmt, args);
    va_end(args);

    if (written < 0) return;
    if ((size_t)written >= writer->size - writer->pos) {
        writer->pos = writer->size - 1;
        writer->buf[writer->pos] = '\0';
        return;
    }
    writer->pos += (size_t)written;
}

static void json_write_string(json_writer_t *writer, const char *value)
{
    static const char hex[] = "0123456789abcdef";
    const unsigned char *p = (const unsigned char *)(value ? value : "");

    json_writef(writer, "\"");
    while (*p && writer->pos + 7 < writer->size) {
        unsigned char c = *p++;
        if (c == '\"' || c == '\\') {
            json_writef(writer, "\\%c", c);
        } else if (c == '\b') {
            json_writef(writer, "\\b");
        } else if (c == '\f') {
            json_writef(writer, "\\f");
        } else if (c == '\n') {
            json_writef(writer, "\\n");
        } else if (c == '\r') {
            json_writef(writer, "\\r");
        } else if (c == '\t') {
            json_writef(writer, "\\t");
        } else if (c < 0x20) {
            char escaped[7] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15], 0};
            json_writef(writer, "%s", escaped);
        } else {
            json_writef(writer, "%c", c);
        }
    }
    json_writef(writer, "\"");
}

void torrent_mgr_status_json(char *buf, size_t bufsz)
{
    if (!buf || bufsz < 64) return;

    json_writer_t writer = {buf, bufsz, 0};
    uint64_t now = get_time_sec();
    json_writef(&writer, "{\"torrents\":[");

    for (int i = 0; i < num_torrents; i++) {
        managed_torrent_t *mt = &torrents[i];

        if (i > 0)
            json_writef(&writer, ",");

        const char *state_str = "stopped";
        switch (mt->state) {
        case TORRENT_STOPPED:     state_str = "stopped"; break;
        case TORRENT_DOWNLOADING: state_str = "downloading"; break;
        case TORRENT_SEEDING:     state_str = "seeding"; break;
        case TORRENT_ERROR:       state_str = "error"; break;
        case TORRENT_DONE:        state_str = "done"; break;
        }

        uint64_t remaining = mt->total_size > mt->downloaded
                           ? mt->total_size - mt->downloaded : 0;
        uint64_t elapsed = mt->start_time > 0 && now >= mt->start_time
                         ? now - mt->start_time : 0;
        size_t pieces_total = mt->torrent ? mt->torrent->num_pieces : 0;
        size_t pieces_done = pieces_total ? mt->piece_mgr.num_complete : 0;

        json_writef(&writer, "{\"id\":%d,\"name\":", i);
        json_write_string(&writer, mt->name);
        json_writef(&writer, ",\"hash\":");
        json_write_string(&writer, mt->id);
        json_writef(&writer,
            ",\"state\":\"%s\","
            "\"progress\":%.3f,"
            "\"size\":%llu,"
            "\"downloaded\":%llu,"
            "\"uploaded\":%llu,"
            "\"remaining\":%llu,"
            "\"speed_down\":%llu,"
            "\"speed_up\":%llu,"
            "\"peers\":%d,"
            "\"active_peers\":%d,"
            "\"pieces_done\":%zu,"
            "\"pieces_total\":%zu,"
            "\"elapsed\":%llu,"
            "\"tracker_announces\":%d,"
            "\"is_magnet\":%s,"
            "\"save_path\":",
            state_str,
            (double)mt->progress,
            (unsigned long long)mt->total_size,
            (unsigned long long)mt->downloaded,
            (unsigned long long)mt->uploaded,
            (unsigned long long)remaining,
            (unsigned long long)mt->speed_down,
            (unsigned long long)mt->speed_up,
            mt->num_peers,
            mt->active_peers,
            pieces_done,
            pieces_total,
            (unsigned long long)elapsed,
            mt->tracker_announces,
            mt->is_magnet ? "true" : "false");
        json_write_string(&writer, mt->save_path);
        json_writef(&writer, ",\"error\":");
        json_write_string(&writer, mt->error_msg[0] ? mt->error_msg : "");
        json_writef(&writer, "}");
    }

    json_writef(&writer, "]}");
}

void torrent_mgr_shutdown(void)
{
    for (int i = num_torrents - 1; i >= 0; i--) {
        torrent_mgr_remove(i);
    }
    num_torrents = 0;
}
