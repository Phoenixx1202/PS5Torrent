#include "net_utils.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

int net_init(void)
{
    /* On FreeBSD (PS5), networking is already initialized.
     * Just return success. */
    return 0;
}

void net_cleanup(void)
{
    /* Nothing to clean up on FreeBSD */
}

int net_resolve(const char *hostname, uint32_t *addr_out)
{
    if (!hostname || !addr_out) return -1;

    /* Try as IP address first */
    struct in_addr in;
    if (inet_pton(AF_INET, hostname, &in) == 1) {
        *addr_out = in.s_addr;
        return 0;
    }

    /* Try DNS resolution */
    struct hostent *he = gethostbyname(hostname);
    if (!he || he->h_addrtype != AF_INET) {
        return -1;
    }

    memcpy(addr_out, he->h_addr_list[0], 4);
    return 0;
}

int net_tcp_connect(uint32_t addr, uint16_t port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = addr;
    saddr.sin_port = htons(port);

    if (connect(sock, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

int net_send_all(int sock, const void *data, size_t len)
{
    const unsigned char *ptr = (const unsigned char *)data;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t sent = send(sock, ptr, remaining, 0);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (sent == 0) return -1;
        ptr += sent;
        remaining -= (size_t)sent;
    }

    return 0;
}

int net_recv_exact(int sock, void *buf, size_t len)
{
    unsigned char *ptr = (unsigned char *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t n = recv(sock, ptr, remaining, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1; // Connection closed
        ptr += n;
        remaining -= (size_t)n;
    }

    return 0;
}

int net_recv_some(int sock, void *buf, size_t len)
{
    ssize_t n = recv(sock, buf, len, 0);
    if (n < 0) {
        if (errno == EINTR) return net_recv_some(sock, buf, len);
        return -1;
    }
    return (int)n;
}

int net_set_timeout(int sock, int timeout_sec)
{
    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        return -1;
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0)
        return -1;

    return 0;
}

void net_close(int sock)
{
    if (sock >= 0) close(sock);
}

void net_url_encode(const unsigned char *input, size_t input_len, char *output)
{
    static const char hex[] = "0123456789abcdef";
    size_t pos = 0;

    for (size_t i = 0; i < input_len; i++) {
        unsigned char c = input[i];
        if ((c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            c == '.' || c == '-' || c == '_' || c == '~') {
            output[pos++] = (char)c;
        } else {
            output[pos++] = '%';
            output[pos++] = hex[c >> 4];
            output[pos++] = hex[c & 0x0F];
        }
    }

    output[pos] = '\0';
}
