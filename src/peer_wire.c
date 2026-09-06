#include "peer_wire.h"
#include "net_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <errno.h>

void peer_handshake_init(peer_handshake_t *hs,
                         const unsigned char info_hash[20],
                         const unsigned char peer_id[20])
{
    memset(hs, 0, sizeof(peer_handshake_t));
    hs->pstrlen = 19;
    memcpy(hs->pstr, "BitTorrent protocol", 19);
    /* Do not advertise extensions/DHT that this client cannot process. */
    memcpy(hs->info_hash, info_hash, 20);
    memcpy(hs->peer_id, peer_id, 20);
}

int peer_handshake(int sock,
                   const unsigned char info_hash[20],
                   const unsigned char peer_id[20],
                   unsigned char peer_id_out[20])
{
    peer_handshake_t hs_out;
    peer_handshake_init(&hs_out, info_hash, peer_id);

    /* Send handshake */
    if (net_send_all(sock, &hs_out, sizeof(peer_handshake_t)) < 0)
        return -1;

    /* Receive handshake */
    peer_handshake_t hs_in;
    if (net_recv_exact(sock, &hs_in, sizeof(peer_handshake_t)) < 0)
        return -1;

    /* Validate handshake */
    if (hs_in.pstrlen != 19) return -1;
    if (memcmp(hs_in.pstr, "BitTorrent protocol", 19) != 0) return -1;
    if (memcmp(hs_in.info_hash, info_hash, 20) != 0) return -1;

    /* Output peer ID if requested */
    if (peer_id_out)
        memcpy(peer_id_out, hs_in.peer_id, 20);

    return 0;
}

int peer_send_interested(int sock)
{
    unsigned char msg[5];
    uint32_t len = 1; // length field
    len = htonl(len);
    memcpy(msg, &len, 4);
    msg[4] = PEER_MSG_INTERESTED;

    return net_send_all(sock, msg, 5);
}

int peer_send_request(int sock, uint32_t index, uint32_t begin, uint32_t length)
{
    unsigned char msg[17];
    uint32_t len = htonl(13); // 1 (id) + 12 (payload)
    memcpy(msg, &len, 4);
    msg[4] = PEER_MSG_REQUEST;

    uint32_t n_index = htonl(index);
    uint32_t n_begin = htonl(begin);
    uint32_t n_length = htonl(length);

    memcpy(msg + 5, &n_index, 4);
    memcpy(msg + 9, &n_begin, 4);
    memcpy(msg + 13, &n_length, 4);

    return net_send_all(sock, msg, 17);
}

int peer_recv_event(int sock, int *type,
                      uint32_t *piece_index,
                      uint32_t *piece_begin,
                      unsigned char *data,
                      uint32_t *data_len,
                      int timeout_sec)
{
    if (type) *type = PEER_MSG_KEEPALIVE;
    if (data_len) *data_len = 0;
    if (timeout_sec == 0) {
        /* A readable socket can contain just one byte. Never consume a
         * partial frame then wait forever for the remainder on the UI thread. */
        uint32_t header;
        ssize_t n = recv(sock, &header, sizeof(header), MSG_PEEK | MSG_DONTWAIT);
        if (n == 0) return -1;
        if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 2 : -1;
        if (n < 4) return 2;
        uint32_t length = ntohl(header);
        if (length > 256 * 1024 + 9) return -1;
        int available = 0;
        if (ioctl(sock, FIONREAD, &available) < 0) return -1;
        if ((uint32_t)available < length + 4) return 2;
    } else if (net_set_timeout(sock, timeout_sec) < 0) return -1;

    /* Read message length (4 bytes) */
    uint32_t net_len;
    if (net_recv_exact(sock, &net_len, 4) < 0)
        return -1;

    uint32_t msg_len = ntohl(net_len);
    if (msg_len > 256 * 1024 + 9) return -1;

    if (msg_len == 0) {
        /* Keep-alive */
        return 1;
    }

    /* Read message ID */
    uint8_t msg_id;
    if (net_recv_exact(sock, &msg_id, 1) < 0)
        return -1;
    if (type) *type = msg_id;
    if (msg_id <= PEER_MSG_NOT_INTERESTED && msg_len != 1) return -1;
    if (msg_id == PEER_MSG_HAVE && msg_len != 5) return -1;
    if ((msg_id == PEER_MSG_REQUEST || msg_id == PEER_MSG_CANCEL) && msg_len != 13) return -1;

    switch (msg_id) {
    case PEER_MSG_CHOKE:
        return 1; // We're choked

    case PEER_MSG_UNCHOKE:
        return 1; // We're unchoked

    case PEER_MSG_INTERESTED:
        return 1;

    case PEER_MSG_NOT_INTERESTED:
        return 1;

    case PEER_MSG_HAVE: {
        /* Read piece index (4 bytes) */
        uint32_t net_idx;
        if (net_recv_exact(sock, &net_idx, 4) < 0) return -1;
        if (piece_index) *piece_index = ntohl(net_idx);
        return 1;
    }

    case PEER_MSG_BITFIELD: {
        /* Read bitfield data */
        uint32_t payload_len = msg_len - 1;
        if (payload_len > 256 * 1024) return -1;
        if (payload_len > 0) {
            if (!data || net_recv_exact(sock, data, payload_len) < 0) return -1;
        }
        if (data_len) *data_len = payload_len;
        return 1;
    }

    case PEER_MSG_PIECE: {
        /* Read piece: index (4) + begin (4) + block data */
        if (msg_len < 9) return -1;

        uint32_t net_idx, net_begin;
        if (net_recv_exact(sock, &net_idx, 4) < 0) return -1;
        if (net_recv_exact(sock, &net_begin, 4) < 0) return -1;

        uint32_t block_len = msg_len - 9;

        if (data && block_len > 0) {
            if (net_recv_exact(sock, data, block_len) < 0)
                return -1;
        } else {
            unsigned char *skip = malloc(block_len);
            if (!skip) return -1;
            net_recv_exact(sock, skip, block_len);
            free(skip);
        }

        if (piece_index) *piece_index = ntohl(net_idx);
        if (piece_begin) *piece_begin = ntohl(net_begin);
        if (data_len) *data_len = block_len;

        return 0; // Piece data received
    }

    case PEER_MSG_CANCEL:
        /* Skip payload */
        if (msg_len > 1) {
            unsigned char *skip = malloc(msg_len - 1);
            if (skip) { net_recv_exact(sock, skip, msg_len - 1); free(skip); }
        }
        return 1;

    default:
        /* Unknown message - skip payload */
        if (msg_len > 1) {
            unsigned char *skip = malloc(msg_len - 1);
            if (skip) { net_recv_exact(sock, skip, msg_len - 1); free(skip); }
        }
        return 1;
    }
}

int peer_recv_message(int sock, uint32_t *index, uint32_t *begin,
                      unsigned char *data, uint32_t *length, int timeout_sec)
{
    return peer_recv_event(sock, NULL, index, begin, data, length, timeout_sec);
}
