#include "torrent_mgr.h"
#include "net_utils.h"
#include "app_log.h"
#include <arpa/inet.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Real loopback peer traffic; only tracker discovery is stubbed. No public
 * swarm, network availability, or long sleeps are required by this test. */
#define PIECE_SIZE 16384
enum { SERVE_PIECE, STALL_AND_CLOSE, DELAY_HANDSHAKE };
typedef struct {
    int listener, mode;
    peer_addr_t addr;
    pthread_t thread;
    atomic_int handshake_seen, release, done;
} test_peer_t;

static unsigned char payload[PIECE_SIZE * 6];
static peer_addr_t advertised[8];
static int advertised_count;
static atomic_int primary_calls, secondary_calls, tracker_inflight;
static atomic_int release_secondary, secondary_finished;

static double now(void)
{
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void tick(void)
{
    double started = now();
    torrent_mgr_tick();
    char status[16384];
    torrent_mgr_status_json(status, sizeof(status));
    assert(now() - started < 0.1); /* A stalled socket must not block the UI. */
}

static void pump(double seconds)
{
    double deadline = now() + seconds;
    do { tick(); usleep(1000); } while (now() < deadline);
}

static void wait_atomic(atomic_int *value)
{
    double deadline = now() + 2;
    while (!atomic_load(value) && now() < deadline) { tick(); usleep(1000); }
    assert(atomic_load(value));
}

tracker_response_t *tracker_announce(const char *url, const tracker_params_t *params)
{
    (void)params;
    atomic_fetch_add(&tracker_inflight, 1);
    if (strstr(url, "secondary")) {
        atomic_fetch_add(&secondary_calls, 1);
        while (!atomic_load(&release_secondary)) usleep(1000);
        atomic_fetch_add(&secondary_finished, 1);
    } else {
        atomic_fetch_add(&primary_calls, 1);
    }
    tracker_response_t *result = calloc(1, sizeof(*result));
    assert(result);
    result->interval = 120;
    result->num_peers = advertised_count;
    result->peers = malloc(sizeof(*result->peers) * (size_t)advertised_count);
    assert(result->peers);
    memcpy(result->peers, advertised, sizeof(*result->peers) * (size_t)advertised_count);
    atomic_fetch_sub(&tracker_inflight, 1);
    return result;
}

void tracker_response_free(tracker_response_t *result)
{
    free(result->peers);
    free(result->failure_reason);
    free(result);
}

void tracker_generate_peer_id(unsigned char *id) { memset(id, 'x', 20); }

static int bound_socket(peer_addr_t *addr, int listen_for_connections)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);
    struct sockaddr_in endpoint = {0};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(sock, (struct sockaddr *)&endpoint, sizeof(endpoint)) == 0);
    socklen_t length = sizeof(endpoint);
    assert(getsockname(sock, (struct sockaddr *)&endpoint, &length) == 0);
    addr->ip = endpoint.sin_addr.s_addr;
    addr->port = endpoint.sin_port;
    if (listen_for_connections) assert(listen(sock, 8) == 0);
    return sock;
}

static void send_message(int sock, unsigned char type, const void *body, uint32_t length)
{
    uint32_t wire_length = htonl(length + 1);
    assert(net_send_all(sock, &wire_length, sizeof(wire_length)) == 0);
    assert(net_send_all(sock, &type, 1) == 0);
    if (length) assert(net_send_all(sock, body, length) == 0);
}

static void *run_peer(void *arg)
{
    test_peer_t *peer = arg;
    int sock = accept(peer->listener, NULL, NULL);
    assert(sock >= 0);
    assert(net_set_timeout(sock, 3) == 0);
    unsigned char handshake[68];
    assert(net_recv_exact(sock, handshake, sizeof(handshake)) == 0);
    assert(handshake[0] == 19);
    assert(memcmp(handshake + 1, "BitTorrent protocol", 19) == 0);
    atomic_store(&peer->handshake_seen, 1);
    if (peer->mode != SERVE_PIECE) {
        while (!atomic_load(&peer->release)) usleep(1000);
        if (peer->mode == STALL_AND_CLOSE) goto done;
    }
    memcpy(handshake + 48, "-TEST00-123456789012", 20);
    if (net_send_all(sock, handshake, sizeof(handshake)) < 0) {
        assert(peer->mode == DELAY_HANDSHAKE);
        goto done;
    }
    if (peer->mode == SERVE_PIECE) {
        const unsigned char bitfield = 0x80; /* Only piece 0: keep torrent active. */
        send_message(sock, PEER_MSG_BITFIELD, &bitfield, 1);
        send_message(sock, PEER_MSG_UNCHOKE, NULL, 0);
    }
    for (;;) {
        unsigned char message[17];
        uint32_t wire_length;
        int rc = net_recv_exact(sock, &wire_length, 4);
        if (rc < 0) {
            assert(errno != EAGAIN && errno != EWOULDBLOCK);
            break;
        }
        uint32_t length = ntohl(wire_length);
        assert(length <= sizeof(message));
        assert(net_recv_exact(sock, message, length) == 0);
        if (length == 1 && message[0] == PEER_MSG_INTERESTED) continue;
        assert(peer->mode == SERVE_PIECE);
        assert(length == 13 && message[0] == PEER_MSG_REQUEST);
        uint32_t fields[3]; memcpy(fields, message + 1, sizeof(fields));
        uint32_t index = ntohl(fields[0]), begin = ntohl(fields[1]);
        uint32_t requested = ntohl(fields[2]);
        assert(index == 0 && begin == 0 && requested == PIECE_SIZE);
        unsigned char block[8 + PIECE_SIZE];
        memcpy(block, fields, 8);
        memcpy(block + 8, payload, PIECE_SIZE);
        send_message(sock, PEER_MSG_PIECE, block, sizeof(block));
    }
done:
    close(sock);
    atomic_store(&peer->done, 1);
    return NULL;
}

static void start_peer(test_peer_t *peer, int mode)
{
    memset(peer, 0, sizeof(*peer));
    atomic_init(&peer->handshake_seen, 0);
    atomic_init(&peer->release, 0);
    atomic_init(&peer->done, 0);
    peer->mode = mode;
    peer->listener = bound_socket(&peer->addr, 1);
    assert(pthread_create(&peer->thread, NULL, run_peer, peer) == 0);
}

static void assert_no_duplicate_connection(test_peer_t *peer)
{
    struct pollfd pending = {peer->listener, POLLIN, 0};
    assert(poll(&pending, 1, 0) == 0);
}

static void finish_peer(test_peer_t *peer)
{
    wait_atomic(&peer->done);
    assert(pthread_join(peer->thread, NULL) == 0);
    assert_no_duplicate_connection(peer);
    close(peer->listener);
}

static torrent_t *make_torrent(int with_secondary)
{
    torrent_t *torrent = calloc(1, sizeof(*torrent));
    assert(torrent);
    torrent->announce = strdup("http://primary/announce");
    torrent->announce_len = strlen(torrent->announce);
    if (with_secondary) {
        torrent->announce_list_count = 1;
        torrent->announce_list = calloc(1, sizeof(char *));
        assert(torrent->announce_list);
        torrent->announce_list[0] = strdup("http://secondary/announce");
    }
    torrent->name = strdup("discovery.bin");
    torrent->name_len = strlen(torrent->name);
    torrent->file_name = strdup(torrent->name);
    torrent->length = torrent->total_size = PIECE_SIZE * 2;
    torrent->piece_length = PIECE_SIZE;
    torrent->num_pieces = 2;
    torrent->pieces_len = 40;
    torrent->pieces = malloc(torrent->pieces_len);
    assert(torrent->pieces);
    for (size_t i = 0; i < 2; i++)
        sha1_hash(payload + i * PIECE_SIZE, PIECE_SIZE, torrent->pieces + i * 20);
    sha1_hash(payload, (size_t)torrent->length, torrent->info_hash);
    return torrent;
}

static peer_candidate_t *candidate(managed_torrent_t *mt, peer_addr_t addr)
{
    for (int i = 0; i < mt->num_candidates; i++) {
        peer_candidate_t *p = &mt->peer_candidates[i];
        if (p->addr.ip == addr.ip && p->addr.port == addr.port) return p;
    }
    assert(!"Missing tracker candidate");
    return NULL;
}

static int fd_count(void)
{
    DIR *dir = opendir("/proc/self/fd");
    assert(dir);
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) if (entry->d_name[0] != '.') count++;
    closedir(dir);
    return count;
}

static void test_progress_and_retries(const char *root)
{
    test_peer_t stalled, good;
    start_peer(&stalled, STALL_AND_CLOSE);
    start_peer(&good, SERVE_PIECE);
    peer_addr_t refused;
    int refusal_socket = bound_socket(&refused, 0); /* Reserve an unused TCP port. */
    advertised[0] = advertised[1] = stalled.addr;
    advertised[2] = advertised[3] = refused;
    advertised[4] = advertised[5] = good.addr;
    advertised_count = 6;
    int id = torrent_mgr_add(make_torrent(1), root, 0, NULL);
    assert(id == 0 && torrent_mgr_start(id) == 0);
    managed_torrent_t *mt = torrent_mgr_get(id);
    double deadline = now() + 1.5;
    while (mt->downloaded == 0 && now() < deadline) { tick(); usleep(1000); }
    assert(mt->downloaded == PIECE_SIZE && mt->piece_mgr.num_complete == 1);
    wait_atomic(&stalled.handshake_seen);
    wait_atomic(&secondary_calls);
    assert(!atomic_load(&secondary_finished));
    assert(!atomic_load(&stalled.done));
    assert(mt->active_peers == 1 && mt->error_type == TERR_NONE);
    assert(mt->num_candidates == 3);
    assert_no_duplicate_connection(&stalled);
    assert_no_duplicate_connection(&good);

    peer_candidate_t *failed = candidate(mt, refused);
    assert(failed->failures == 1);
    uint64_t first_retry = failed->retry_after;
    assert(first_retry >= (uint64_t)now() + 28);
    atomic_store(&release_secondary, 1);
    wait_atomic(&secondary_finished);
    pump(0.05);
    assert(mt->tracker_interval == 120);

    /* Stalling for 31 seconds must not override the tracker's 120s interval. */
    int announces = atomic_load(&primary_calls);
    mt->last_announce_time = (uint64_t)now() - 31;
    mt->last_progress_time = (uint64_t)now() - 31;
    pump(0.05);
    assert(atomic_load(&primary_calls) == announces);
    /* Force a normal announce due, still within the failed peer's cooldown. */
    mt->last_announce_time = (uint64_t)now() - 121;
    deadline = now() + 1;
    while (atomic_load(&primary_calls) == announces && now() < deadline) { tick(); usleep(1000); }
    assert(atomic_load(&primary_calls) == announces + 1);
    pump(0.05);
    assert(mt->num_candidates == 3);
    assert(failed->failures == 1 && failed->retry_after == first_retry);
    assert_no_duplicate_connection(&stalled);
    assert_no_duplicate_connection(&good);
    assert(mt->error_type == TERR_NONE);

    /* Advance only the candidate's deadline to exercise backoff without
     * spending several minutes asleep. Repeated tracker lists above must not
     * have reset its failure history. */
    const unsigned delays[] = {60, 120, 240, 300, 300};
    for (unsigned i = 0; i < sizeof(delays) / sizeof(delays[0]); i++) {
        unsigned failures = failed->failures;
        uint64_t due = (uint64_t)now() - 1;
        failed->retry_after = due;
        deadline = now() + 1;
        while (failed->retry_after == due && now() < deadline) { tick(); usleep(1000); }
        assert(failed->failures >= failures); /* Counter may saturate at the cap. */
        if (delays[i] < 300) assert(failed->failures == failures + 1);
        assert(failed->retry_after >= (uint64_t)now() + delays[i] - 1);
        assert(failed->retry_after <= (uint64_t)now() + delays[i] + 1);
        failures = failed->failures;
        pump(0.01);
        assert(failed->failures == failures);
    }

    atomic_store(&stalled.release, 1);
    finish_peer(&stalled);
    torrent_mgr_remove(id);
    finish_peer(&good);
    close(refusal_socket);
    pump(0.05);
    assert(atomic_load(&tracker_inflight) == 0);
    puts("Peer discovery: immediate data, responsive ticks, endpoint deduplication, tracker interval and capped retry backoff passed");
}

static void test_late_connection(const char *root, int remove)
{
    test_peer_t delayed;
    start_peer(&delayed, DELAY_HANDSHAKE);
    advertised[0] = delayed.addr;
    advertised_count = 1;
    int id = torrent_mgr_add(make_torrent(0), root, 0, NULL);
    assert(id == 0 && torrent_mgr_start(id) == 0);
    wait_atomic(&delayed.handshake_seen);
    if (remove) {
        torrent_mgr_remove(id);
        id = torrent_mgr_add(make_torrent(0), root, 0, NULL);
        assert(id == 0); /* Reuse the same slot and info hash with a new generation. */
    } else {
        torrent_mgr_stop(id);
    }
    atomic_store(&delayed.release, 1);
    finish_peer(&delayed);
    pump(0.05);
    managed_torrent_t *mt = torrent_mgr_get(id);
    assert(mt->state == TORRENT_STOPPED);
    assert(mt->active_peers == 0 && mt->num_peers == 0 && mt->downloaded == 0);
    if (remove) assert(mt->tracker_announces == 0 && mt->num_candidates == 0);
    torrent_mgr_remove(id);
    pump(0.05);
    assert(atomic_load(&tracker_inflight) == 0);
    puts(remove ? "Peer discovery: late handshake cannot revive a removed/reused slot passed"
                : "Peer discovery: late handshake cannot revive a stopped torrent passed");
}

typedef struct {
    int listener;
    peer_addr_t addr;
    pthread_t thread;
    atomic_int requested, release_data, release_tail, done;
} test_web_seed_t;

static void *run_web_seed(void *arg)
{
    test_web_seed_t *seed = arg;
    /* Six pieces form two batches. Return the first accepted batch, and hold
     * the other batch open so the torrent remains active after real progress. */
    for (int request = 0; request < 2; request++) {
        int sock = accept(seed->listener, NULL, NULL);
        assert(sock >= 0 && net_set_timeout(sock, 3) == 0);
        char header[4096]; size_t used = 0;
        do {
            assert(used + 1 < sizeof(header));
            assert(net_recv_exact(sock, header + used++, 1) == 0);
            header[used] = 0;
        } while (!strstr(header, "\r\n\r\n"));
        char *range = strstr(header, "Range: bytes=");
        assert(range);
        unsigned long long first, last;
        assert(sscanf(range, "Range: bytes=%llu-%llu", &first, &last) == 2);
        assert(first <= last && last < sizeof(payload));
        atomic_fetch_add(&seed->requested, 1);
        if (request == 0) {
            while (!atomic_load(&seed->release_data)) usleep(1000);
            int count = snprintf(header, sizeof(header),
                "HTTP/1.0 206 Partial Content\r\nContent-Length: %llu\r\n\r\n", last - first + 1);
            assert(net_send_all(sock, header, (size_t)count) == 0);
            assert(net_send_all(sock, payload + first, (size_t)(last - first + 1)) == 0);
        } else {
            while (!atomic_load(&seed->release_tail)) usleep(1000);
        }
        close(sock);
    }
    atomic_store(&seed->done, 1);
    return NULL;
}

static void test_web_seed_status(const char *root)
{
    test_web_seed_t seed = {0};
    atomic_init(&seed.requested, 0);
    atomic_init(&seed.release_data, 0);
    atomic_init(&seed.release_tail, 0);
    atomic_init(&seed.done, 0);
    seed.listener = bound_socket(&seed.addr, 1);
    assert(pthread_create(&seed.thread, NULL, run_web_seed, &seed) == 0);
    int refusal_socket = bound_socket(&advertised[0], 0);
    advertised_count = 1;
    torrent_t *torrent = make_torrent(0);
    torrent->length = torrent->total_size = sizeof(payload);
    torrent->num_pieces = 6;
    torrent->pieces_len = 6 * 20;
    free(torrent->pieces);
    torrent->pieces = malloc(torrent->pieces_len);
    assert(torrent->pieces);
    for (size_t i = 0; i < 6; i++)
        sha1_hash(payload + i * PIECE_SIZE, PIECE_SIZE, torrent->pieces + i * 20);
    torrent->web_seeds = calloc(1, sizeof(char *));
    assert(torrent->web_seeds);
    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%u/discovery.bin", ntohs(seed.addr.port));
    torrent->web_seeds[0] = strdup(url);
    torrent->web_seed_count = 1;
    int id = torrent_mgr_add(torrent, root, 0, NULL);
    assert(id == 0 && torrent_mgr_start(id) == 0);
    managed_torrent_t *mt = torrent_mgr_get(id);
    wait_atomic(&seed.requested);
    double deadline = now() + 1;
    while (mt->error_type != TERR_NO_PEERS && now() < deadline) { tick(); usleep(1000); }
    assert(mt->error_type == TERR_NO_PEERS && mt->downloaded == 0);
    atomic_store(&seed.release_data, 1);
    deadline = now() + 1;
    while (mt->downloaded == 0 && now() < deadline) { tick(); usleep(1000); }
    assert(mt->downloaded > 0 && mt->downloaded < mt->total_size);
    assert(mt->active_peers == 0 && mt->error_type == TERR_NONE);
    assert(mt->error_msg[0] == 0);
    /* A hung source must eventually show a warning again. */
    mt->last_progress_time = (uint64_t)now() - 61;
    tick(); assert(mt->error_type == TERR_NO_PEERS);
    mt->last_progress_time = (uint64_t)now();
    tick(); assert(mt->error_type == TERR_NONE && mt->error_msg[0] == 0);
    torrent_mgr_remove(id);
    atomic_store(&seed.release_tail, 1);
    wait_atomic(&seed.done);
    assert(pthread_join(seed.thread, NULL) == 0);
    close(seed.listener);
    close(refusal_socket);
    pump(0.05);
    puts("Peer discovery: recent verified web seed data suppresses peer warning, stale progress restores it passed");
}

int main(void)
{
    alarm(15);
    char root[] = "/tmp/ps5torrent-discovery-XXXXXX";
    assert(mkdtemp(root));
    for (size_t i = 0; i < sizeof(payload); i++) payload[i] = (unsigned char)(i * 17 + 3);
    app_log_open(root);
    int baseline_fds = fd_count();
    torrent_mgr_init();
    test_progress_and_retries(root);
    test_late_connection(root, 0);
    test_late_connection(root, 1);
    test_web_seed_status(root);
    torrent_mgr_shutdown();
    pump(0.05);
    assert(fd_count() == baseline_fds);
    app_log_close();
    char path[512];
    snprintf(path, sizeof(path), "%s/discovery.bin", root); unlink(path);
    snprintf(path, sizeof(path), "%s/PS5Torrent.log", root); unlink(path);
    assert(rmdir(root) == 0);
    puts("Peer discovery: no leaked sockets passed");
    return 0;
}
