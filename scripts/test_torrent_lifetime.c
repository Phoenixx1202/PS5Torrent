#include "torrent_mgr.h"
#include "http_server.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>

/* A tracker deliberately stalled until the UI-side test releases it. */
static atomic_int entered, released;
tracker_response_t *tracker_announce(const char *url, const tracker_params_t *params)
{
    (void)url; (void)params;
    atomic_store(&entered, 1);
    while (!atomic_load(&released)) usleep(1000);
    tracker_response_t *result = calloc(1, sizeof(*result));
    result->interval = 30;
    return result;
}
void tracker_response_free(tracker_response_t *r) { free(r); }
void tracker_generate_peer_id(unsigned char *id) { memset(id, 'x', 20); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

int main(int argc, char **argv)
{
    alarm(8);
    (void)argv;
    const char *data = argc > 1
        ? "d8:announce12:udp://x:80/a4:infod6:lengthi4e4:name8:test.bin12:piece lengthi4e6:pieces20:aaaaaaaaaaaaaaaaaaaaee"
        : "d8:announce9:http://x/4:infod6:lengthi4e4:name8:test.bin12:piece lengthi4e6:pieces20:aaaaaaaaaaaaaaaaaaaaee";
    char root[] = "/tmp/ps5torrent-life-XXXXXX"; assert(mkdtemp(root));
    torrent_mgr_init();
    const unsigned char invalid[] = "d4:infod4:name1:xee";
    assert(torrent_mgr_add_raw(invalid, sizeof(invalid) - 1, root) == -1);
    assert(torrent_mgr_count() == 0);
    int id = torrent_mgr_add_raw((const unsigned char *)data, strlen(data), root); assert(id == 0);
    assert(!torrent_mgr_start(id));
    assert(!torrent_mgr_start(id));
    double start = now(); assert(torrent_mgr_tick() == 1); assert(now() - start < 0.1);
    for (int i = 0; i < 1000 && !atomic_load(&entered); i++) usleep(1000);
    assert(atomic_load(&entered));
    char status[16384];
    start = now();
    for (int i = 0; i < 100; i++) { torrent_mgr_tick(); torrent_mgr_status_json(status, sizeof(status)); }
    assert(now() - start < 0.2 && strstr(status, "test.bin"));
    torrent_mgr_remove(id); /* Must not free memory still used by the worker. */
    assert(torrent_mgr_count() == 0);
    id = torrent_mgr_add_raw((const unsigned char *)data, strlen(data), root); assert(id == 0);
    atomic_store(&released, 1);
    for (int i = 0; i < 100; i++) { usleep(1000); torrent_mgr_tick(); }
    assert(torrent_mgr_get(id)->state == TORRENT_STOPPED);
    assert(torrent_mgr_get(id)->tracker_announces == 0);
    torrent_mgr_shutdown();
    char path[512]; snprintf(path, sizeof(path), "%s/test.bin", root); unlink(path); rmdir(root);

    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    uint32_t wire = 0, index, begin, length; unsigned char block[256 * 1024];
    assert(write(pair[1], &wire, 1) == 1);
    start = now(); assert(peer_recv_message(pair[0], &index, &begin, block, &length, 0) == 2);
    assert(now() - start < 0.1);
    assert(write(pair[1], ((char *)&wire) + 1, 3) == 3);
    assert(peer_recv_message(pair[0], &index, &begin, block, &length, 0) == 1);
    wire = htonl(0xffffffff); assert(write(pair[1], &wire, 4) == 4);
    assert(peer_recv_message(pair[0], &index, &begin, block, &length, 0) == -1);
    close(pair[0]); close(pair[1]);
    puts("Torrent lifetime: stalled tracker, responsive status, removal during discovery, fragmented and oversized peer frames passed");
}
