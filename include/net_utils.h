#ifndef PS5TORRENT_NET_UTILS_H
#define PS5TORRENT_NET_UTILS_H

#include <stdint.h>
#include <stddef.h>

/**
 * Maximum buffer sizes.
 */
#define NET_BUF_SIZE    65536   // 64KB recv buffer
#define HTTP_BUF_SIZE   8192    // HTTP response buffer

/**
 * Initialize networking subsystem.
 * @return 0 on success, -1 on error
 */
int net_init(void);

/**
 * Shutdown networking subsystem.
 */
void net_cleanup(void);

/**
 * Resolve a hostname to an IP address (IPv4).
 * @param hostname  Hostname or IP string
 * @param addr_out  Output: 4-byte IPv4 address (network byte order)
 * @return 0 on success, -1 on error
 */
int net_resolve(const char *hostname, uint32_t *addr_out);

/**
 * Create a TCP socket and connect to a remote host.
 * @param addr  Remote IP address (network byte order)
 * @param port  Remote port (host byte order)
 * @return Socket fd on success, -1 on error
 */
int net_tcp_connect(uint32_t addr, uint16_t port);

/**
 * Send all data (handles partial sends).
 * @param sock  Socket fd
 * @param data  Data to send
 * @param len   Length of data
 * @return 0 on success, -1 on error
 */
int net_send_all(int sock, const void *data, size_t len);

/**
 * Receive exactly len bytes (blocking).
 * @param sock  Socket fd
 * @param buf   Receive buffer
 * @param len   Number of bytes to receive
 * @return 0 on success, -1 on error/timeout
 */
int net_recv_exact(int sock, void *buf, size_t len);

/**
 * Receive up to len bytes.
 * @param sock  Socket fd
 * @param buf   Receive buffer
 * @param len   Maximum bytes to receive
 * @return Number of bytes received, 0 on close, -1 on error
 */
int net_recv_some(int sock, void *buf, size_t len);

/**
 * Set socket timeout.
 * @param sock        Socket fd
 * @param timeout_sec Timeout in seconds
 * @return 0 on success, -1 on error
 */
int net_set_timeout(int sock, int timeout_sec);

/**
 * Close a socket.
 */
void net_close(int sock);

/**
 * URL encode a binary buffer.
 * @param input     Input data
 * @param input_len Input length
 * @param output    Output buffer (must be at least input_len*3 + 1)
 */
void net_url_encode(const unsigned char *input, size_t input_len, char *output);

#endif /* PS5TORRENT_NET_UTILS_H */
