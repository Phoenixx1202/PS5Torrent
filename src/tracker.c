#include "tracker.h"
#include "net_utils.h"
#include "bencode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/**
 * Parse HTTP response, extract headers and body.
 */
static int parse_http_response(const unsigned char *resp, size_t resp_len,
                               int *status_code,
                               const unsigned char **body, size_t *body_len)
{
    /* Find "HTTP/1.x " and extract status code */
    const char *p = (const char *)resp;
    const char *end = p + resp_len;

    /* Skip HTTP version */
    while (p < end && *p != ' ') p++;
    if (p >= end) return -1;
    p++; // skip space

    /* Read status code */
    if (p + 3 > end) return -1;
    *status_code = (p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0');

    /* Find double CRLF (empty line separating headers from body) */
    const char *body_start = NULL;
    for (const char *s = p; s + 3 < end; s++) {
        if (s[0] == '\r' && s[1] == '\n' && s[2] == '\r' && s[3] == '\n') {
            body_start = s + 4;
            break;
        }
    }

    if (!body_start) return -1;

    *body = (const unsigned char *)body_start;
    *body_len = (size_t)(end - body_start);

    return 0;
}

tracker_response_t *tracker_announce(const char *tracker_url,
                                     const tracker_params_t *params)
{
    if (!tracker_url || !params) return NULL;

    /* Parse tracker URL: http://host:port/announce */
    const char *url = tracker_url;

    /* Check for http:// */
    if (strncmp(url, "http://", 7) != 0) return NULL;
    url += 7;

    /* Find host[:port]/path */
    const char *path_start = strchr(url, '/');
    if (!path_start) return NULL;

    /* Parse host and optional port */
    char host[256];
    int port = 80; // default HTTP port
    const char *port_str = NULL;
    size_t host_len;

    const char *colon = memchr(url, ':', (size_t)(path_start - url));
    if (colon) {
        host_len = (size_t)(colon - url);
        port_str = colon + 1;
        port = 0;
        while (*port_str >= '0' && *port_str <= '9' && port_str < path_start) {
            port = port * 10 + (*port_str++ - '0');
        }
    } else {
        host_len = (size_t)(path_start - url);
    }

    if (host_len >= sizeof(host)) return NULL;
    memcpy(host, url, host_len);
    host[host_len] = '\0';

    /* Build announce URL parameters */
    char info_hash_enc[100];
    net_url_encode(params->info_hash, 20, info_hash_enc);

    char peer_id_enc[100];
    net_url_encode(params->peer_id, 20, peer_id_enc);

    char request[4096];
    int req_len = snprintf(request, sizeof(request),
        "GET %s%cinfo_hash=%s"
        "&peer_id=%s"
        "&port=%u"
        "&uploaded=%lld"
        "&downloaded=%lld"
        "&left=%lld"
        "&compact=%d"
        "&numwant=%d"
        " HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: PS5Torrent/1.0\r\n"
        "Connection: close\r\n"
        "\r\n",
        path_start,
        strchr(path_start, '?') ? '&' : '?',
        info_hash_enc,
        peer_id_enc,
        (unsigned int)params->port,
        (long long)params->uploaded,
        (long long)params->downloaded,
        (long long)params->left,
        params->compact ? 1 : 0,
        MAX_TRACKER_PEERS,
        host);

    if (req_len < 0 || (size_t)req_len >= sizeof(request)) return NULL;

    /* Resolve host and connect */
    uint32_t addr;
    if (net_resolve(host, &addr) < 0) return NULL;

    int sock = net_tcp_connect(addr, (uint16_t)port);
    if (sock < 0) return NULL;

    /* Set timeout */
    net_set_timeout(sock, 30);

    /* Send HTTP request */
    if (net_send_all(sock, request, (size_t)req_len) < 0) {
        net_close(sock);
        return NULL;
    }

    /* Receive response (use a large buffer) */
    unsigned char resp_buf[HTTP_BUF_SIZE * 4];
    size_t resp_pos = 0;

    while (resp_pos < sizeof(resp_buf)) {
        int n = net_recv_some(sock, resp_buf + resp_pos,
                              sizeof(resp_buf) - resp_pos);
        if (n <= 0) break;
        resp_pos += (size_t)n;
    }

    net_close(sock);

    if (resp_pos == 0) return NULL;

    /* Parse HTTP response */
    int status_code;
    const unsigned char *body;
    size_t body_len;

    if (parse_http_response(resp_buf, resp_pos, &status_code, &body, &body_len) < 0)
        return NULL;

    tracker_response_t *tr = calloc(1, sizeof(tracker_response_t));
    if (!tr) return NULL;

    if (status_code != 200) {
        /* Try to extract failure reason */
        bcode_node_t *root = bcode_parse(body, body_len);
        if (root) {
            bcode_node_t *fr = bcode_dict_get(root, "failure reason");
            if (fr && fr->type == BCODE_STR) {
                tr->failure_reason = malloc(fr->str_len + 1);
                if (tr->failure_reason) {
                    memcpy(tr->failure_reason, fr->str_val, fr->str_len);
                    tr->failure_reason[fr->str_len] = '\0';
                }
            }
            bcode_free(root);
        }
        if (!tr->failure_reason) {
            tr->failure_reason = strdup("Unknown tracker error");
        }
        return tr;
    }

    /* Parse bencoded tracker response */
    bcode_node_t *root = bcode_parse(body, body_len);
    if (!root) {
        tr->failure_reason = strdup("Failed to parse tracker response");
        return tr;
    }

    /* Check for failure */
    bcode_node_t *fr = bcode_dict_get(root, "failure reason");
    if (fr && fr->type == BCODE_STR) {
        tr->failure_reason = malloc(fr->str_len + 1);
        if (tr->failure_reason) {
            memcpy(tr->failure_reason, fr->str_val, fr->str_len);
            tr->failure_reason[fr->str_len] = '\0';
        }
        bcode_free(root);
        return tr;
    }

    /* Parse interval */
    bcode_node_t *interval = bcode_dict_get(root, "interval");
    if (interval) tr->interval = (int)bcode_int_val(interval);
    if (tr->interval < 60) tr->interval = 60; // Minimum 60 seconds

    /* Parse complete/incomplete */
    bcode_node_t *comp = bcode_dict_get(root, "complete");
    if (comp) tr->complete = (int)bcode_int_val(comp);

    bcode_node_t *incomp = bcode_dict_get(root, "incomplete");
    if (incomp) tr->incomplete = (int)bcode_int_val(incomp);

    /* Parse peers */
    bcode_node_t *peers = bcode_dict_get(root, "peers");
    if (peers && peers->type == BCODE_STR) {
        /* Compact response: 6 bytes per peer (4 IP + 2 port) */
        int num_peers = (int)(peers->str_len / 6);
        if (num_peers > MAX_TRACKER_PEERS) num_peers = MAX_TRACKER_PEERS;

        tr->peers = calloc((size_t)num_peers, sizeof(peer_addr_t));
        if (tr->peers) {
            tr->num_peers = num_peers;
            for (int i = 0; i < num_peers; i++) {
                memcpy(&tr->peers[i].ip, peers->str_val + (size_t)i * 6, 4);
                memcpy(&tr->peers[i].port, peers->str_val + (size_t)i * 6 + 4, 2);
            }
        }
    }

    bcode_free(root);
    return tr;
}

void tracker_response_free(tracker_response_t *resp)
{
    if (!resp) return;
    free(resp->peers);
    free(resp->failure_reason);
    free(resp);
}

void tracker_generate_peer_id(unsigned char peer_id[20])
{
    /* Azureus-style peer ID: -PS5T- + random 12 hex digits */
    static const char hex[] = "0123456789abcdef";
    peer_id[0] = '-';
    peer_id[1] = 'P';
    peer_id[2] = 'S';
    peer_id[3] = '5';
    peer_id[4] = 'T';
    peer_id[5] = '-';

    /* Simple pseudo-random - use address or other entropy sources */
    for (int i = 6; i < 20; i++) {
        /* In a real implementation, use a proper PRNG.
         * For the PS5, we use a simple linear congruential generator */
        static unsigned int seed = 0x12345678;
        seed = seed * 1103515245 + 12345;
        peer_id[i] = hex[(seed >> 16) & 0x0F];
    }
}
