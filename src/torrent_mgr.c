#include "torrent_mgr.h"
#include "net_utils.h"
#include "sha1.h"
#include "http_server.h"
#include "app_log.h"
#include "web_seed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdatomic.h>
#include <fcntl.h>

/**
 * Managed torrents array.
 */
static managed_torrent_t torrents[MAX_TORRENTS];
static int num_torrents = 0;
static uint64_t next_generation = 1;
static size_t assembly_bytes;
#define ASSEMBLY_LIMIT (64u * 1024u * 1024u)
#define CHOKED_PEER_TIMEOUT 45u
#define PEER_CONNECT_PARALLEL 4
#define PEER_CONNECT_MAX_JOBS (MAX_TORRENTS * PEER_CONNECT_PARALLEL)
#define WEB_SEED_PARALLEL_PER_TORRENT 4
#define WEB_SEED_BATCH_PIECES 4
#define WEB_SEED_MAX_JOBS (MAX_TORRENTS * WEB_SEED_PARALLEL_PER_TORRENT)

static void record_peer_failure(managed_torrent_t *mt, peer_addr_t addr);

static void release_piece(managed_torrent_t *mt, torrent_peer_t *p)
{
    if (p->piece_data) {
        if (mt->piece_mgr.pieces && p->last_request < mt->piece_mgr.num_pieces &&
            mt->piece_mgr.pieces[p->last_request].state != PIECE_COMPLETE)
            mt->piece_mgr.pieces[p->last_request].state = PIECE_FREE;
        assembly_bytes -= p->piece_size;
        free(p->piece_data);
    }
    p->piece_data = NULL; p->piece_size = 0;
    p->piece_received = p->pending_length = 0;
}

static void disconnect_peer(managed_torrent_t *mt, torrent_peer_t *p, const char *reason)
{
    if (p->sock >= 0) {
        if (mt->state == TORRENT_DOWNLOADING) record_peer_failure(mt, p->addr);
        net_close(p->sock); p->sock = -1; mt->active_peers--;
    }
    release_piece(mt, p);
    free(p->bitfield); p->bitfield = NULL; p->bitfield_len = 0;
    app_log_write("INFO", reason);
}

static void peer_addr_text(peer_addr_t addr, char *out, size_t out_size)
{
    char ip[INET_ADDRSTRLEN] = {0};
    struct in_addr in;
    in.s_addr = addr.ip;
    inet_ntop(AF_INET, &in, ip, sizeof(ip));
    snprintf(out, out_size, "%s:%u", ip[0] ? ip : "0.0.0.0", ntohs(addr.port));
}

static torrent_t *torrent_clone_for_worker(const torrent_t *source)
{
    if (!source) return NULL;
    torrent_t *copy = calloc(1, sizeof(*copy));
    if (!copy) return NULL;
    memcpy(copy->info_hash, source->info_hash, 20);
    copy->piece_length = source->piece_length;
    copy->pieces_len = source->pieces_len;
    copy->num_pieces = source->num_pieces;
    copy->is_multi_file = source->is_multi_file;
    copy->total_size = source->total_size;
    copy->length = source->length;
    copy->num_files = source->num_files;
    if (source->announce && !(copy->announce = strdup(source->announce))) goto fail;
    copy->announce_len = source->announce_len;
    if (source->announce_list_count) {
        copy->announce_list = calloc(source->announce_list_count, sizeof(char *));
        if (!copy->announce_list) goto fail;
        for (size_t i = 0; i < source->announce_list_count; i++) {
            copy->announce_list[i] = strdup(source->announce_list[i]);
            if (!copy->announce_list[i]) goto fail;
            copy->announce_list_count++;
        }
    }
    if (source->web_seed_count) {
        copy->web_seeds = calloc(source->web_seed_count, sizeof(char *));
        if (!copy->web_seeds) goto fail;
        for (size_t i = 0; i < source->web_seed_count; i++) {
            copy->web_seeds[i] = strdup(source->web_seeds[i]);
            if (!copy->web_seeds[i]) goto fail;
            copy->web_seed_count++;
        }
    }
    if (source->name && !(copy->name = strdup(source->name))) goto fail;
    copy->name_len = source->name_len;
    if (source->file_name && !(copy->file_name = strdup(source->file_name))) goto fail;
    if (source->pieces_len) {
        copy->pieces = malloc(source->pieces_len);
        if (!copy->pieces) goto fail;
        memcpy(copy->pieces, source->pieces, source->pieces_len);
    }
    if (source->num_files) {
        copy->files = calloc(source->num_files, sizeof(*copy->files));
        if (!copy->files) goto fail;
        for (size_t i = 0; i < source->num_files; i++) {
            copy->files[i].length = source->files[i].length;
            copy->files[i].path_len = source->files[i].path_len;
            if (source->files[i].path && !(copy->files[i].path = strdup(source->files[i].path))) goto fail;
        }
    }
    return copy;
fail:
    torrent_free(copy);
    return NULL;
}

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
    mt->generation = next_generation++;

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
    int index = torrent_mgr_add(t, save_path, 0, NULL);
    if (index < 0) torrent_free(t);
    return index;
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
    app_log_write("INFO", "Torrent start requested");
    if (index < 0 || index >= num_torrents) return -1;
    managed_torrent_t *mt = &torrents[index];

    if (mt->state == TORRENT_DOWNLOADING || mt->state == TORRENT_SEEDING ||
        mt->state == TORRENT_DONE) {
        mt->error_type = TERR_NONE;
        mt->error_msg[0] = '\0';
        app_log_write("INFO", "Torrent start ignored because it is already active or complete");
        return 0;
    }

    if (mt->state != TORRENT_STOPPED && mt->state != TORRENT_ERROR)
        return -1;
    if (!mt->torrent) return -1;

    if (mt->piece_mgr.pieces || mt->file_writer.base_path) {
        torrent_mgr_stop(index);
        piece_mgr_destroy(&mt->piece_mgr);
        file_writer_destroy(&mt->file_writer);
    }
    mt->downloaded = 0;
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
    if (!mt->piece_mgr.pieces || !mt->piece_mgr.bitfield) {
        mt->state = TORRENT_ERROR;
        mt->error_type = TERR_PARSE_FAILED;
        snprintf(mt->error_msg, sizeof(mt->error_msg), "Not enough memory for torrent pieces");
        return -1;
    }

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
    app_log_write("INFO", "Torrent initialized; download engine active");
    mt->start_time = get_time_sec();
    mt->last_announce_time = 0;
    mt->last_progress_time = mt->start_time;
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
    mt->generation = next_generation++;
    for (int i = 0; i < mt->num_peers; i++) {
        disconnect_peer(mt, &mt->peers[i], "Peer closed: torrent stopped");
    }
    mt->num_peers = 0;
    mt->active_peers = 0;
    mt->num_candidates = mt->next_candidate = 0;
    mt->tracker_failures = 0;
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
typedef void (*candidate_sink_t)(void *, const peer_addr_t *, int);

static int announce_to_tracker(managed_torrent_t *mt, candidate_sink_t publish, void *context)
{
    if (!mt->torrent->announce && mt->torrent->announce_list_count == 0)
        return -1;

    tracker_params_t params;
    params.info_hash = mt->torrent->info_hash;
    params.peer_id = mt->peer_id;
    params.port = 6881;
    params.uploaded = (int64_t)mt->uploaded;
    params.downloaded = (int64_t)mt->downloaded;
    params.left = (int64_t)(mt->total_size - mt->downloaded);
    params.compact = 1;

    int contacted = 0;
    int compatible = 0, https_skipped = 0;
    int peers_reported = 0;
    mt->tracker_announces++;
    mt->tracker_interval = 30;

    /* Try the primary URL and every tier from announce-list. Many torrents
     * keep a dead primary tracker but contain working HTTP fallbacks. */
    size_t url_count = mt->torrent->announce_list_count +
                       (mt->torrent->announce ? 1u : 0u);
    for (size_t url_index = 0; url_index < url_count; url_index++) {
        const char *url = url_index == 0 && mt->torrent->announce
                        ? mt->torrent->announce
                        : mt->torrent->announce_list[
                            url_index - (mt->torrent->announce ? 1u : 0u)];
        if (!url) continue;
        if (strncmp(url, "http://", 7) && strncmp(url, "udp://", 6)) {
            if (!strncmp(url, "https://", 8)) https_skipped++;
            app_log_write("WARN", "Tracker skipped: unsupported scheme (HTTPS is not implemented)");
            continue;
        }
        if (mt->torrent->announce && url != mt->torrent->announce &&
            strcmp(url, mt->torrent->announce) == 0) continue;

        char tracker_log[384];
        /* Log endpoint only: announce paths/queries can contain private keys. */
        const char *endpoint = strstr(url, "://") + 3;
        size_t endpoint_len = strcspn(endpoint, "/?#");
        snprintf(tracker_log, sizeof(tracker_log), "Tracker request: protocol=%s, endpoint=%.*s",
                 !strncmp(url, "udp://", 6) ? "UDP" : "HTTP", (int)(endpoint_len > 256 ? 256 : endpoint_len), endpoint);
        app_log_write("INFO", tracker_log);
        compatible++;
        tracker_response_t *tr = tracker_announce(url, &params);
        if (!tr) { app_log_write("WARN", "Tracker request failed: no valid response"); continue; }
        if (tr->failure_reason) {
            app_log_write("WARN", "Tracker returned a failure response");
            tracker_response_free(tr);
            continue;
        }
        contacted++;
        if (tr->interval > mt->tracker_interval)
            mt->tracker_interval = tr->interval;
        peers_reported += tr->num_peers;
        snprintf(tracker_log, sizeof(tracker_log), "Tracker response: peers=%d, seeders=%d, leechers=%d, interval=%d",
                 tr->num_peers, tr->complete, tr->incomplete, tr->interval);
        app_log_write("INFO", tracker_log);

        /* Publish immediately: slow peers and later trackers must not delay
         * use of addresses returned by this tracker. */
        if (tr->peers && tr->num_peers > 0)
            publish(context, tr->peers, tr->num_peers);
        tracker_response_free(tr);
    }

    if (peers_reported > 0) {
        mt->error_type = TERR_NONE;
        mt->error_msg[0] = '\0';
        return 0;
    }

    if (contacted == 0) {
        snprintf(mt->error_msg, sizeof(mt->error_msg),
                 "%s", compatible
                 ? "Falha ao consultar os trackers. Nova tentativa automática; consulte o log para detalhes."
                 : https_skipped
                   ? "Este torrent só tem trackers HTTPS ou incompatíveis; HTTPS ainda não é suportado."
                   : "Este torrent não tem trackers HTTP/UDP compatíveis.");
    } else {
        snprintf(mt->error_msg, sizeof(mt->error_msg),
                 "Trackers responderam, mas não há peers disponíveis; nova tentativa automática");
    }
    mt->error_type = TERR_NO_PEERS;
    return -1;
}

/**
 * Process one peer for a torrent - try to recv messages and send requests.
 */
/* Tracker workers publish addresses; connection workers own new sockets.
 * Only the main thread touches live torrents. Generation IDs reject stale work. */
typedef struct {
    managed_torrent_t result;
    pthread_mutex_t queue_mutex;
    peer_addr_t queue[MAX_TRACKER_PEERS];
    int queued;
    atomic_int done;
} discovery_job_t;
static discovery_job_t *discovery_jobs[MAX_TORRENTS];

typedef struct {
    uint64_t generation;
    peer_addr_t addr;
    unsigned char info_hash[20], peer_id[20], remote_id[20];
    int sock;
    int error;
    const char *stage;
    atomic_int done;
} peer_connect_job_t;
static peer_connect_job_t *peer_connect_jobs[PEER_CONNECT_MAX_JOBS];

typedef struct {
    uint64_t generation;
    size_t first_piece;
    size_t piece_count;
    size_t piece_sizes[WEB_SEED_BATCH_PIECES];
    size_t total_size;
    unsigned char *data;
    torrent_t *torrent;
    int ok;
    atomic_int done;
} web_seed_job_t;
static web_seed_job_t *web_seed_jobs[WEB_SEED_MAX_JOBS];

static void free_discovery(discovery_job_t *job)
{
    pthread_mutex_destroy(&job->queue_mutex);
    torrent_free(job->result.torrent);
    free(job);
}

static int same_peer(peer_addr_t a, peer_addr_t b)
{
    return a.ip == b.ip && a.port == b.port;
}

static int peer_is_connected(const managed_torrent_t *mt, peer_addr_t addr)
{
    for (int i = 0; i < mt->num_peers; i++)
        if (mt->peers[i].sock >= 0 && same_peer(mt->peers[i].addr, addr)) return 1;
    return 0;
}

static peer_candidate_t *find_candidate(managed_torrent_t *mt, peer_addr_t addr)
{
    for (int i = 0; i < mt->num_candidates; i++)
        if (same_peer(mt->peer_candidates[i].addr, addr)) return &mt->peer_candidates[i];
    return NULL;
}

static unsigned retry_delay(unsigned failures)
{
    unsigned delay = 30u << (failures > 4 ? 4 : failures ? failures - 1 : 0);
    return delay > 300 ? 300 : delay;
}

static void record_peer_failure(managed_torrent_t *mt, peer_addr_t addr)
{
    peer_candidate_t *candidate = find_candidate(mt, addr);
    if (!candidate) return;
    if (candidate->failures < 5) candidate->failures++;
    candidate->retry_after = get_time_sec() + retry_delay(candidate->failures);
}

static void add_candidate(managed_torrent_t *mt, peer_addr_t addr)
{
    if (!addr.ip || addr.ip == UINT32_MAX || !addr.port || find_candidate(mt, addr)) return;
    int slot = mt->num_candidates;
    if (slot == MAX_TRACKER_PEERS) {
        slot = -1;
        for (int i = 0; i < mt->num_candidates; i++) {
            peer_candidate_t *c = &mt->peer_candidates[i];
            if (!c->connecting && c->failures && !peer_is_connected(mt, c->addr) &&
                (slot < 0 || c->failures > mt->peer_candidates[slot].failures)) slot = i;
        }
        if (slot < 0) return;
    } else mt->num_candidates++;
    memset(&mt->peer_candidates[slot], 0, sizeof(mt->peer_candidates[slot]));
    mt->peer_candidates[slot].addr = addr;
}

static void publish_candidates(void *context, const peer_addr_t *peers, int count)
{
    discovery_job_t *job = context;
    pthread_mutex_lock(&job->queue_mutex);
    for (int i = 0; i < count && job->queued < MAX_TRACKER_PEERS; i++) {
        int duplicate = 0;
        for (int j = 0; j < job->queued; j++)
            if (same_peer(job->queue[j], peers[i])) { duplicate = 1; break; }
        if (!duplicate) job->queue[job->queued++] = peers[i];
    }
    pthread_mutex_unlock(&job->queue_mutex);
}

static void *discover_peers(void *arg)
{
    discovery_job_t *job = arg;
    app_log_write("INFO", "Tracker discovery started in background");
    announce_to_tracker(&job->result, publish_candidates, job);
    char summary[192];
    snprintf(summary, sizeof(summary), "Tracker discovery finished: torrent=%s, error_code=%d",
             job->result.id, job->result.error_type);
    app_log_write("INFO", summary);
    atomic_store(&job->done, 1);
    return NULL;
}

static void collect_discovery(void)
{
    for (int j = 0; j < MAX_TORRENTS; j++) {
        discovery_job_t *job = discovery_jobs[j];
        if (!job) continue;
        int done = atomic_load(&job->done);
        managed_torrent_t *live = NULL;
        for (int i = 0; i < num_torrents; i++)
            if (torrents[i].generation == job->result.generation &&
                torrents[i].state == TORRENT_DOWNLOADING) live = &torrents[i];
        peer_addr_t pending[MAX_TRACKER_PEERS];
        pthread_mutex_lock(&job->queue_mutex);
        int count = job->queued;
        memcpy(pending, job->queue, (size_t)count * sizeof(*pending));
        job->queued = 0;
        pthread_mutex_unlock(&job->queue_mutex);
        if (live)
            for (int i = 0; i < count; i++) add_candidate(live, pending[i]);
        if (!done) continue;
        if (live) {
            live->tracker_announces = job->result.tracker_announces;
            live->tracker_interval = job->result.tracker_interval;
            live->last_announce_time = get_time_sec();
            if (job->result.error_type != TERR_NONE) {
                if (live->tracker_failures < 5) live->tracker_failures++;
                unsigned delay = retry_delay(live->tracker_failures);
                if (live->tracker_interval < (int)delay) live->tracker_interval = (int)delay;
            } else live->tracker_failures = 0;
            if (!live->active_peers && !live->num_candidates &&
                (live->error_type == TERR_NONE || live->error_type == TERR_NO_PEERS)) {
                live->error_type = job->result.error_type;
                snprintf(live->error_msg, sizeof(live->error_msg), "%s", job->result.error_msg);
            }
        }
        discovery_jobs[j] = NULL;
        free_discovery(job);
    }
}

static void schedule_discovery(managed_torrent_t *mt)
{
    if (!mt->torrent->announce && !mt->torrent->announce_list_count) return;
    int slot = -1;
    for (int i = 0; i < MAX_TORRENTS; i++) {
        if (discovery_jobs[i] && discovery_jobs[i]->result.generation == mt->generation) return;
        if (!discovery_jobs[i]) slot = i;
    }
    if (slot < 0) return;
    discovery_job_t *job = calloc(1, sizeof(*job));
    if (!job) return;
    if (pthread_mutex_init(&job->queue_mutex, NULL)) { free(job); return; }
    atomic_init(&job->done, 0);
    job->result = *mt;
    /* Workers must never retain ownership of live peer buffers. */
    memset(job->result.peers, 0, sizeof(job->result.peers));
    job->result.num_peers = job->result.active_peers = 0;
    job->result.torrent = torrent_clone_for_worker(mt->torrent);
    if (!job->result.torrent) { free_discovery(job); return; }
    pthread_t thread;
    pthread_attr_t attr;
    if (pthread_attr_init(&attr)) goto fail;
    int rc = pthread_attr_setstacksize(&attr, 1024 * 1024);
    if (!rc) rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (!rc) rc = pthread_create(&thread, &attr, discover_peers, job);
    pthread_attr_destroy(&attr);
    if (rc) goto fail;
    discovery_jobs[slot] = job;
    mt->last_announce_time = get_time_sec();
    if (mt->tracker_interval <= 0) mt->tracker_interval = 30;
    return;
fail:
    free_discovery(job);
}

static void *connect_candidate(void *arg)
{
    peer_connect_job_t *job = arg;
    job->stage = "TCP connect";
    job->sock = net_tcp_connect(job->addr.ip, ntohs(job->addr.port));
    if (job->sock < 0) goto fail;
    job->stage = "handshake timeout setup";
    if (net_set_timeout(job->sock, 15) < 0) goto fail;
    job->stage = "BitTorrent handshake";
    if (peer_handshake(job->sock, job->info_hash, job->peer_id, job->remote_id) < 0) goto fail;
    job->stage = "interested message";
    if (peer_send_interested(job->sock) < 0) goto fail;
    job->stage = "nonblocking setup";
    int flags = fcntl(job->sock, F_GETFL, 0);
    if (flags < 0 || fcntl(job->sock, F_SETFL, flags | O_NONBLOCK) < 0) goto fail;
    atomic_store(&job->done, 1);
    return NULL;
fail:
    job->error = errno;
    if (job->sock >= 0) net_close(job->sock);
    job->sock = -1;
    atomic_store(&job->done, 1);
    return NULL;
}

static void collect_peer_connections(void)
{
    for (int j = 0; j < PEER_CONNECT_MAX_JOBS; j++) {
        peer_connect_job_t *job = peer_connect_jobs[j];
        if (!job || !atomic_load(&job->done)) continue;
        managed_torrent_t *live = NULL;
        for (int i = 0; i < num_torrents; i++)
            if (torrents[i].generation == job->generation &&
                torrents[i].state == TORRENT_DOWNLOADING) live = &torrents[i];
        if (live) {
            peer_candidate_t *candidate = find_candidate(live, job->addr);
            if (candidate) candidate->connecting = 0;
            char endpoint[64], message[384];
            peer_addr_text(job->addr, endpoint, sizeof(endpoint));
            if (job->sock < 0) {
                record_peer_failure(live, job->addr);
                snprintf(message, sizeof(message),
                         "Peer connection failed: endpoint=%s, stage=%s, errno=%d (%s), retry_in=%u seconds",
                         endpoint, job->stage, job->error, strerror(job->error),
                         retry_delay(candidate ? candidate->failures : 1));
                app_log_write("WARN", message);
            } else if (!peer_is_connected(live, job->addr)) {
                int slot = -1;
                for (int i = 0; i < live->num_peers; i++)
                    if (live->peers[i].sock < 0) { slot = i; break; }
                if (slot < 0 && live->num_peers < MAX_PEERS_PER_TORRENT) slot = live->num_peers++;
                if (slot >= 0) {
                    torrent_peer_t *peer = &live->peers[slot];
                    memset(peer, 0, sizeof(*peer));
                    peer->sock = job->sock;
                    peer->addr = job->addr;
                    memcpy(peer->peer_id, job->remote_id, 20);
                    peer->choked = peer->am_interested = 1;
                    peer->last_activity = get_time_sec();
                    job->sock = -1;
                    live->active_peers++;
                    snprintf(message, sizeof(message),
                             "Peer ready for transfer: endpoint=%s, active_peers=%d", endpoint, live->active_peers);
                    app_log_write("INFO", message);
                }
            }
        }
        if (job->sock >= 0) net_close(job->sock);
        peer_connect_jobs[j] = NULL;
        free(job);
    }
}

static void schedule_peer_connections(managed_torrent_t *mt)
{
    int pending = 0;
    for (int j = 0; j < PEER_CONNECT_MAX_JOBS; j++)
        if (peer_connect_jobs[j] && peer_connect_jobs[j]->generation == mt->generation) pending++;
    uint64_t now = get_time_sec();
    for (int checked = 0; checked < mt->num_candidates && pending < PEER_CONNECT_PARALLEL &&
         mt->active_peers + pending < MAX_PEERS_PER_TORRENT; checked++) {
        int index = mt->next_candidate++ % mt->num_candidates;
        mt->next_candidate %= mt->num_candidates;
        peer_candidate_t *candidate = &mt->peer_candidates[index];
        if (candidate->connecting || now < candidate->retry_after ||
            peer_is_connected(mt, candidate->addr)) continue;
        int slot = -1;
        for (int j = 0; j < PEER_CONNECT_MAX_JOBS; j++)
            if (!peer_connect_jobs[j]) { slot = j; break; }
        if (slot < 0) break;
        peer_connect_job_t *job = calloc(1, sizeof(*job));
        if (!job) break;
        job->generation = mt->generation;
        job->addr = candidate->addr;
        job->sock = -1;
        memcpy(job->info_hash, mt->info_hash, 20);
        memcpy(job->peer_id, mt->peer_id, 20);
        atomic_init(&job->done, 0);
        pthread_t thread;
        pthread_attr_t attr;
        int rc = pthread_attr_init(&attr);
        if (!rc) {
            rc = pthread_attr_setstacksize(&attr, 256 * 1024);
            if (!rc) rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            if (!rc) rc = pthread_create(&thread, &attr, connect_candidate, job);
            pthread_attr_destroy(&attr);
        }
        if (rc) {
            free(job);
            candidate->retry_after = now + 5;
            app_log_write("WARN", "Peer connection worker unavailable; retrying later");
            break;
        }
        peer_connect_jobs[slot] = job;
        candidate->connecting = 1;
        pending++;
        char endpoint[64], message[128];
        peer_addr_text(candidate->addr, endpoint, sizeof(endpoint));
        snprintf(message, sizeof(message), "Peer connection attempt: endpoint=%s", endpoint);
        app_log_write("INFO", message);
    }
}

static void update_peer_status(managed_torrent_t *mt)
{
    if (mt->error_type != TERR_NONE && mt->error_type != TERR_NO_PEERS) return;
    uint64_t now = get_time_sec();
    int pending = 0;
    uint64_t next_retry = UINT64_MAX;
    for (int i = 0; i < mt->num_candidates; i++) {
        peer_candidate_t *c = &mt->peer_candidates[i];
        pending += c->connecting;
        if (c->retry_after < next_retry) next_retry = c->retry_after;
    }
    if (mt->active_peers || pending || (mt->downloaded && now - mt->last_progress_time < 60)) {
        mt->error_type = TERR_NONE;
        mt->error_msg[0] = 0;
    } else if (mt->num_candidates && next_retry > now) {
        mt->error_type = TERR_NO_PEERS;
        snprintf(mt->error_msg, sizeof(mt->error_msg),
                 "Peers indisponíveis; nova tentativa em %llu s. Consulte o log para detalhes.",
                 (unsigned long long)(next_retry - now));
    }
}

static void free_web_seed_job(web_seed_job_t *job)
{
    if (!job) return;
    torrent_free(job->torrent);
    free(job->data);
    free(job);
}

static void *fetch_web_seed_piece(void *arg)
{
    web_seed_job_t *job = arg;
    job->ok = web_seed_fetch_pieces(job->torrent, job->first_piece,
                                    job->piece_count, job->piece_sizes,
                                    job->data) == 0;
    atomic_store(&job->done, 1);
    return NULL;
}

static size_t torrent_piece_size(const torrent_t *torrent, size_t index)
{
    if (!torrent || index >= torrent->num_pieces) return 0;
    uint64_t size = (uint64_t)torrent->piece_length;
    if (index == torrent->num_pieces - 1)
        size = torrent->total_size - (uint64_t)index * size;
    return (size_t)size;
}

static void collect_web_seed(void)
{
    for (int j = 0; j < WEB_SEED_MAX_JOBS; j++) {
        web_seed_job_t *job = web_seed_jobs[j];
        if (!job || !atomic_load(&job->done)) continue;
        managed_torrent_t *live = NULL;
        for (int i = 0; i < num_torrents; i++)
            if (torrents[i].generation == job->generation &&
                torrents[i].state == TORRENT_DOWNLOADING) live = &torrents[i];
        if (live && job->first_piece < live->piece_mgr.num_pieces) {
            if (job->ok) {
                size_t offset = 0;
                size_t saved = 0;
                int failed = 0;
                for (size_t piece = 0; piece < job->piece_count; piece++) {
                    size_t index = job->first_piece + piece;
                    size_t size = job->piece_sizes[piece];
                    if (index >= live->piece_mgr.num_pieces ||
                        live->piece_mgr.pieces[index].state == PIECE_COMPLETE) {
                        offset += size;
                        continue;
                    }
                    unsigned char hash[20];
                    sha1_hash(job->data + offset, size, hash);
                    if (memcmp(hash, live->torrent->pieces + index * 20, 20) != 0 ||
                        file_writer_write_piece(&live->file_writer, job->data + offset,
                                                size, index) != 0 ||
                        piece_mgr_complete(&live->piece_mgr, index,
                                           job->data + offset, size) != 0) {
                        live->piece_mgr.pieces[index].state = PIECE_FREE;
                        failed = 1;
                        break;
                    }
                    live->downloaded += size;
                    saved++;
                    offset += size;
                }
                if (saved) live->last_progress_time = get_time_sec();
                if (failed) {
                    for (size_t piece = saved; piece < job->piece_count; piece++) {
                        size_t index = job->first_piece + piece;
                        if (index < live->piece_mgr.num_pieces &&
                            live->piece_mgr.pieces[index].state != PIECE_COMPLETE)
                            live->piece_mgr.pieces[index].state = PIECE_FREE;
                    }
                    app_log_write("WARN", "Web seed batch failed validation or disk write");
                } else {
                    char message[192];
                    snprintf(message, sizeof(message),
                             "Web seed batch verified and saved: first=%zu, pieces=%zu, bytes=%zu, completed=%zu/%zu",
                             job->first_piece, saved, job->total_size,
                             live->piece_mgr.num_complete, live->torrent->num_pieces);
                    app_log_write("INFO", message);
                }
            } else {
                for (size_t piece = 0; piece < job->piece_count; piece++) {
                    size_t index = job->first_piece + piece;
                    if (index < live->piece_mgr.num_pieces &&
                        live->piece_mgr.pieces[index].state != PIECE_COMPLETE)
                        live->piece_mgr.pieces[index].state = PIECE_FREE;
                }
                app_log_write("WARN", "Web seed batch request failed");
            }
        }
        web_seed_jobs[j] = NULL;
        free_web_seed_job(job);
    }
}

static int web_seed_active_count(const managed_torrent_t *mt)
{
    int count = 0;
    for (int i = 0; i < WEB_SEED_MAX_JOBS; i++)
        if (web_seed_jobs[i] && web_seed_jobs[i]->generation == mt->generation)
            count++;
    return count;
}

static int schedule_web_seed_one(managed_torrent_t *mt)
{
    if (!mt->torrent || !mt->torrent->web_seed_count)
        return 0;
    if (web_seed_active_count(mt) >= WEB_SEED_PARALLEL_PER_TORRENT)
        return 0;
    int slot = -1;
    for (int i = 0; i < WEB_SEED_MAX_JOBS; i++)
        if (!web_seed_jobs[i]) { slot = i; break; }
    if (slot < 0) return 0;
    int next = piece_mgr_get_next(&mt->piece_mgr, NULL, 0);
    if (next < 0) return 0;
    size_t first_piece = (size_t)next;
    size_t piece_count = 0;
    size_t piece_sizes[WEB_SEED_BATCH_PIECES] = {0};
    size_t total_size = 0;
    for (size_t i = 0; i < WEB_SEED_BATCH_PIECES; i++) {
        size_t index = first_piece + i;
        if (index >= mt->torrent->num_pieces) break;
        if (mt->piece_mgr.pieces[index].state != PIECE_FREE) break;
        size_t size = torrent_piece_size(mt->torrent, index);
        if (!size || total_size + size > ASSEMBLY_LIMIT) break;
        piece_sizes[piece_count++] = size;
        total_size += size;
    }
    if (!piece_count || !total_size) return 0;
    web_seed_job_t *job = calloc(1, sizeof(*job));
    if (!job) return 0;
    job->data = malloc(total_size);
    job->torrent = torrent_clone_for_worker(mt->torrent);
    if (!job->data || !job->torrent) { free_web_seed_job(job); return 0; }
    job->generation = mt->generation;
    job->first_piece = first_piece;
    job->piece_count = piece_count;
    job->total_size = total_size;
    memcpy(job->piece_sizes, piece_sizes, sizeof(piece_sizes));
    atomic_init(&job->done, 0);
    for (size_t i = 0; i < piece_count; i++)
        piece_mgr_request(&mt->piece_mgr, first_piece + i);
    pthread_t thread;
    pthread_attr_t attr;
    if (pthread_attr_init(&attr)) {
        for (size_t i = 0; i < piece_count; i++)
            mt->piece_mgr.pieces[first_piece + i].state = PIECE_FREE;
        free_web_seed_job(job);
        return 0;
    }
    int rc = pthread_attr_setstacksize(&attr, 1024 * 1024);
    if (!rc) rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (!rc) rc = pthread_create(&thread, &attr, fetch_web_seed_piece, job);
    pthread_attr_destroy(&attr);
    if (rc) {
        for (size_t i = 0; i < piece_count; i++)
            mt->piece_mgr.pieces[first_piece + i].state = PIECE_FREE;
        free_web_seed_job(job);
        return 0;
    }
    web_seed_jobs[slot] = job;
    return 1;
}

static void schedule_web_seed(managed_torrent_t *mt)
{
    while (schedule_web_seed_one(mt)) {}
}

static void process_peer(managed_torrent_t *mt, int peer_idx)
{
    torrent_peer_t *p = &mt->peers[peer_idx];
    if (p->sock < 0 || mt->state != TORRENT_DOWNLOADING) return;
    static unsigned char data[256 * 1024];
    uint64_t now = get_time_sec();
    if (!p->last_activity) p->last_activity = now;
    if (p->choked && !p->pending_length && now - p->last_activity >= CHOKED_PEER_TIMEOUT) {
        disconnect_peer(mt, p, "Peer stayed choked too long; retry scheduled with cooldown");
        return;
    }
    if ((p->pending_length && now - p->request_time >= 60) ||
        now - p->last_activity >= 180) {
        disconnect_peer(mt, p, "Peer timed out; incomplete piece released for retry"); return;
    }
    for (int event = 0; event < 8; event++) {
        uint32_t index = 0, begin = 0, length = 0;
        int type;
        int ret = peer_recv_event(p->sock, &type, &index, &begin, data, &length, 0);
        if (ret == 2) break;
        if (ret < 0) {
            disconnect_peer(mt, p, "Peer disconnected or sent an invalid frame"); return;
        }
        p->last_activity = now;
        if (type == PEER_MSG_CHOKE) {
            p->choked = 1;
            p->pending_length = 0;
            app_log_write("INFO", "Peer choked the download; waiting for unchoke");
        } else if (type == PEER_MSG_UNCHOKE) {
            p->choked = 0;
            app_log_write("INFO", "Peer unchoked; block requests enabled");
        } else if (type == PEER_MSG_BITFIELD || type == PEER_MSG_HAVE) {
            size_t needed = (mt->torrent->num_pieces + 7) / 8;
            if ((type == PEER_MSG_BITFIELD && length != needed) ||
                (type == PEER_MSG_HAVE && index >= mt->torrent->num_pieces)) {
                disconnect_peer(mt, p, "Peer sent invalid piece availability"); return;
            }
            if (!p->bitfield) {
                p->bitfield = calloc(needed, 1); p->bitfield_len = needed;
                if (!p->bitfield) { disconnect_peer(mt, p, "Peer closed: bitfield allocation failed"); return; }
            }
            if (type == PEER_MSG_BITFIELD) memcpy(p->bitfield, data, needed);
            else p->bitfield[index / 8] |= 1u << (7 - index % 8);
        } else if (type == PEER_MSG_PIECE) {
            /* Ignore late replies to cancelled requests; accept only the exact
             * outstanding block, never arbitrary offsets or duplicate bytes. */
            if (!p->piece_data || !p->pending_length) continue;
            if (index != p->last_request || begin != p->piece_received) continue;
            if (length != p->pending_length || length > p->piece_size - p->piece_received) {
                disconnect_peer(mt, p, "Peer sent an invalid block length"); return;
            }
            memcpy(p->piece_data + begin, data, length);
            p->piece_received += length; p->pending_length = 0;
            mt->last_progress_time = now;
            if (p->piece_received == length)
                app_log_write("INFO", "First block received for piece; assembling 16 KiB blocks");
            if (p->piece_received == p->piece_size) {
                unsigned char hash[20]; sha1_hash(p->piece_data, p->piece_size, hash);
                if (memcmp(hash, mt->torrent->pieces + (size_t)index * 20, 20)) {
                    disconnect_peer(mt, p, "Piece hash mismatch; peer closed and piece scheduled for retry"); return;
                }
                if (file_writer_write_piece(&mt->file_writer, p->piece_data, p->piece_size, index) < 0) {
                    mt->state = TORRENT_ERROR; mt->error_type = TERR_WRITE_FAILED;
                    snprintf(mt->error_msg, sizeof(mt->error_msg), "Failed to write downloaded piece (errno=%d)", errno);
                    app_log_write("ERROR", mt->error_msg);
                    disconnect_peer(mt, p, "Peer closed after disk write failure"); return;
                }
                piece_mgr_complete(&mt->piece_mgr, index, p->piece_data, p->piece_size);
                mt->downloaded += p->piece_size; p->downloaded += p->piece_size;
                peer_candidate_t *candidate = find_candidate(mt, p->addr);
                if (candidate) { candidate->failures = 0; candidate->retry_after = 0; }
                char message[160];
                snprintf(message, sizeof(message), "Piece verified and saved: index=%u, bytes=%zu, completed=%zu/%zu",
                         index, p->piece_size, mt->piece_mgr.num_complete, mt->torrent->num_pieces);
                app_log_write("INFO", message);
                release_piece(mt, p);
            }
        }
    }
    if (p->choked || p->pending_length || !p->bitfield) return;
    if (!p->piece_data) {
        int next = piece_mgr_get_next(&mt->piece_mgr, p->bitfield, p->bitfield_len);
        if (next < 0) return;
        uint64_t size = (uint64_t)mt->torrent->piece_length;
        if ((size_t)next == mt->torrent->num_pieces - 1)
            size = mt->torrent->total_size - (uint64_t)next * size;
        if (size > ASSEMBLY_LIMIT) {
            mt->state = TORRENT_ERROR; mt->error_type = TERR_PARSE_FAILED;
            snprintf(mt->error_msg, sizeof(mt->error_msg), "Piece size exceeds the 64 MiB assembly limit");
            app_log_write("ERROR", mt->error_msg); return;
        }
        if (size > ASSEMBLY_LIMIT - assembly_bytes) return;
        p->piece_data = malloc((size_t)size);
        if (!p->piece_data) { disconnect_peer(mt, p, "Peer closed: piece allocation failed"); return; }
        p->piece_size = (size_t)size; assembly_bytes += p->piece_size;
        p->piece_received = 0; p->last_request = (uint32_t)next;
        piece_mgr_request(&mt->piece_mgr, (size_t)next);
    }
    size_t remaining = p->piece_size - p->piece_received;
    uint32_t length = remaining > PEER_BLOCK_SIZE ? PEER_BLOCK_SIZE : (uint32_t)remaining;
    if (peer_send_request(p->sock, p->last_request, p->piece_received, length) < 0) {
        disconnect_peer(mt, p, "Peer block request failed; piece released for retry"); return;
    }
    p->pending_length = length; p->request_time = now;
}

int torrent_mgr_tick(void)
{
    collect_web_seed();
    collect_discovery();
    collect_peer_connections();
    int active = 0;
    uint64_t now = get_time_sec();

    for (int i = 0; i < num_torrents; i++) {
        managed_torrent_t *mt = &torrents[i];
        if (mt->state != TORRENT_DOWNLOADING) continue;

        active++;

        /* Check if complete */
        if (piece_mgr_is_done(&mt->piece_mgr)) {
            for (int p = 0; p < mt->num_peers; p++)
                disconnect_peer(mt, &mt->peers[p], "Peer closed: download completed");
            file_writer_finish(&mt->file_writer);
            mt->state = TORRENT_DONE;
            mt->progress = 1.0f;
            mt->speed_down = 0;
            mt->speed_up = 0;
            app_log_write("SUCCESS", "Download completed; all pieces verified and saved");
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

        /* Peer retries use cached addresses. Tracker requests keep their own
         * interval and are timed from completion, not a stalled worker's start. */
        if (mt->last_announce_time == 0 || (mt->tracker_interval > 0 &&
            now - mt->last_announce_time >= (uint64_t)mt->tracker_interval)) {
            schedule_discovery(mt);
        }

        /* Run the send scheduler even when no new bytes arrived. */
        for (int p = 0; p < mt->num_peers; p++) process_peer(mt, p);

        schedule_peer_connections(mt);
        schedule_web_seed(mt);
        update_peer_status(mt);
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
