#include "peer_wire.h"
#include "net_utils.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const unsigned char info_hash[20] = "torrent-under-test";
static const unsigned char local_id[20] = "local-peer-id";
static const unsigned char remote_id[20] = "remote-peer-id";

static void check_handshake(int scenario)
{
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(net_set_timeout(pair[0], 1) == 0);
    peer_handshake_t reply;
    peer_handshake_init(&reply, info_hash, remote_id);
    size_t reply_len = sizeof(reply);
    int expected_error = 0;
    switch (scenario) {
    case 1: /* Reject a one-byte non-BitTorrent prefix without a timeout. */
        reply.pstrlen = 'H';
        reply_len = 1;
        expected_error = EPROTO;
        break;
    case 2: /* A complete but incorrect protocol name. */
        reply.pstr[0] = 'X';
        reply_len = 20;
        expected_error = EPROTO;
        break;
    case 3: /* A valid protocol for another torrent. */
        reply.info_hash[0] ^= 1;
        expected_error = EPROTO;
        break;
    case 4: /* The endpoint closes before completing the handshake. */
        reply_len = 27;
        expected_error = ECONNRESET;
        break;
    }
    assert(net_send_all(pair[1], &reply, reply_len) == 0);
    if (scenario == 4) assert(shutdown(pair[1], SHUT_WR) == 0);

    unsigned char received_id[20];
    errno = EINPROGRESS; /* What a successful nonblocking connect can leave. */
    int result = peer_handshake(pair[0], info_hash, local_id, received_id);
    if (expected_error) {
        assert(result == -1);
        assert(errno == expected_error);
    } else {
        assert(result == 0);
        assert(memcmp(received_id, remote_id, sizeof(remote_id)) == 0);
    }
    peer_handshake_t sent;
    assert(net_recv_exact(pair[1], &sent, sizeof(sent)) == 0);
    assert(sent.pstrlen == 19);
    assert(memcmp(sent.pstr, "BitTorrent protocol", 19) == 0);
    assert(memcmp(sent.info_hash, info_hash, sizeof(info_hash)) == 0);
    assert(memcmp(sent.peer_id, local_id, sizeof(local_id)) == 0);
    close(pair[0]);
    close(pair[1]);
}

int main(void)
{
    alarm(10);
    for (int scenario = 0; scenario < 5; ++scenario) check_handshake(scenario);
    puts("Peer transport tests passed");
    return 0;
}
