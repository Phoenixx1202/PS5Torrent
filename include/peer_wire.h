#ifndef PS5TORRENT_PEER_WIRE_H
#define PS5TORRENT_PEER_WIRE_H

#include <stdint.h>
#include <stddef.h>
#include "sha1.h"
#include "tracker.h"

/**
 * Peer wire protocol constants.
 */
#define PEER_HANDSHAKE_LEN  68
#define PEER_BLOCK_SIZE     (16 * 1024)  // 16KB blocks
#define PEER_MAX_REQUESTS   5             // Pipelined requests
#define PEER_MSG_KEEPALIVE  -1
#define PEER_MSG_CHOKE      0
#define PEER_MSG_UNCHOKE    1
#define PEER_MSG_INTERESTED 2
#define PEER_MSG_NOT_INTERESTED 3
#define PEER_MSG_HAVE       4
#define PEER_MSG_BITFIELD   5
#define PEER_MSG_REQUEST    6
#define PEER_MSG_PIECE      7
#define PEER_MSG_CANCEL     8

/**
 * A peer wire message header.
 */
typedef struct {
    uint32_t length;    // Message length (excluding this field)
    uint8_t  id;        // Message ID
} __attribute__((packed)) peer_msg_header_t;

/**
 * Handshake structure.
 */
typedef struct {
    uint8_t  pstrlen;                           // Always 19
    uint8_t  pstr[19];                          // "BitTorrent protocol"
    uint8_t  reserved[8];                       // Reserved bits
    uint8_t  info_hash[SHA1_DIGEST_SIZE];       // 20-byte info hash
    uint8_t  peer_id[20];                       // 20-byte peer ID
} __attribute__((packed)) peer_handshake_t;

/**
 * Initialize a handshake message.
 */
void peer_handshake_init(peer_handshake_t *hs,
                         const unsigned char info_hash[20],
                         const unsigned char peer_id[20]);

/**
 * Perform a BitTorrent handshake with a peer.
 * @param sock      Connected socket
 * @param info_hash 20-byte info hash
 * @param peer_id   20-byte local peer ID
 * @param peer_id_out Output buffer for remote peer's ID (20 bytes), or NULL
 * @return 0 on success, -1 on error
 */
int peer_handshake(int sock,
                   const unsigned char info_hash[20],
                   const unsigned char peer_id[20],
                   unsigned char peer_id_out[20]);

/**
 * Send an "interested" message.
 */
int peer_send_interested(int sock);

/**
 * Send a "request" message for a block.
 */
int peer_send_request(int sock, uint32_t index, uint32_t begin, uint32_t length);

/**
 * Read and process incoming messages from a peer.
 * @param sock          Connected socket
 * @param piece_index   Output: piece index
 * @param piece_begin   Output: offset within piece
 * @param data          Output buffer for piece data
 * @param data_len      Output: length of received data
 * @param timeout_sec   Receive timeout
 * @return 0 for piece data, 1 for control, 2 for incomplete frame, -1 for error/disconnect
 */
int peer_recv_message(int sock,
                      uint32_t *piece_index,
                      uint32_t *piece_begin,
                      unsigned char *data,
                      uint32_t *data_len,
                      int timeout_sec);

/* Typed variant: data needs 256 KiB; bitfields are returned in data as well. */
int peer_recv_event(int sock, int *type, uint32_t *index, uint32_t *begin,
                    unsigned char *data, uint32_t *length, int timeout_sec);

#endif /* PS5TORRENT_PEER_WIRE_H */
