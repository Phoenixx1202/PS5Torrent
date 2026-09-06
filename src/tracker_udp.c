#include "tracker.h"
#include "net_utils.h"
#include "app_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

static void put32(unsigned char *p, uint32_t v)
{ p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static uint32_t get32(const unsigned char *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void put64(unsigned char *p, uint64_t v)
{ put32(p, (uint32_t)(v>>32)); put32(p+4, (uint32_t)v); }
static int counter(const unsigned char *p)
{ uint32_t v=get32(p); return v>INT_MAX ? INT_MAX : (int)v; }
static int64_t milliseconds(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000; }

/* A connected UDP socket also filters datagrams from other endpoints. Ignore
 * stale transactions without extending the deadline. Retry once after 15s;
 * the manager schedules a fresh discovery after this bounded attempt. */
static int exchange(int sock, const unsigned char *request, size_t length,
                    unsigned char *response, size_t capacity, uint32_t action)
{
    for (int attempt=0; attempt<2; attempt++) {
        if (send(sock, request, length, 0)!=(ssize_t)length) return -1;
        int64_t deadline=milliseconds()+(15000<<attempt);
        for (;;) {
            int64_t left=deadline-milliseconds();
            if (left<=0) break;
            struct timeval timeout={ (long)(left/1000), (long)((left%1000)*1000) };
            fd_set readable; FD_ZERO(&readable); FD_SET(sock, &readable);
            int ready=select(sock+1, &readable, NULL, NULL, &timeout);
            if (ready<0 && errno==EINTR) continue;
            if (ready<0) return -1;
            if (!ready) break;
            ssize_t n=recv(sock, response, capacity, 0);
            if (n<0 && errno==EINTR) continue;
            if (n<0) return -1;
            if (n<8 || get32(response+4)!=get32(request+12)) continue;
            if (get32(response)==3) {
                app_log_write("ERROR", "UDP tracker rejected the request");
                return -1;
            }
            if (get32(response)!=action) continue;
            if (n<(action==0 ? 16 : 20)) return -1;
            return (int)n;
        }
    }
    app_log_write("WARN", "UDP tracker response timed out after retry");
    return -1;
}

tracker_response_t *tracker_announce_udp(const char *url, const tracker_params_t *params)
{
    if (!url || strncmp(url,"udp://",6) || !params || !params->info_hash || !params->peer_id) return NULL;
    const char *host_start=url+6, *colon=strchr(host_start, ':');
    if (!colon || colon==host_start || (size_t)(colon-host_start)>=256) return NULL;
    char host[256]; memcpy(host,host_start,(size_t)(colon-host_start)); host[colon-host_start]=0;
    char *end; errno=0; long port=strtol(colon+1,&end,10);
    if (errno || end==colon+1 || (*end && *end!='/') || port<1 || port>65535) return NULL;
    uint32_t ip;
    if (net_resolve(host,&ip)<0) { app_log_write("ERROR","UDP tracker DNS resolution failed"); return NULL; }
    int sock=socket(AF_INET,SOCK_DGRAM,0);
    if (sock<0) return NULL;
    if (sock>=FD_SETSIZE) { net_close(sock); return NULL; }
    struct sockaddr_in address={0}; address.sin_family=AF_INET;
    address.sin_addr.s_addr=ip; address.sin_port=htons((uint16_t)port);
    tracker_response_t *result=NULL;
    if (connect(sock,(struct sockaddr *)&address,sizeof(address))<0) goto done;
    unsigned char request[98]={0}, response[65536];
    put64(request,UINT64_C(0x41727101980));
    put32(request+12,arc4random());
    if (exchange(sock,request,16,response,sizeof(response),0)<0) goto done;
    memcpy(request,response+8,8);
    put32(request+8,1); put32(request+12,arc4random());
    memcpy(request+16,params->info_hash,20); memcpy(request+36,params->peer_id,20);
    put64(request+56,(uint64_t)params->downloaded); put64(request+64,(uint64_t)params->left);
    put64(request+72,(uint64_t)params->uploaded);
    /* Event=none is valid for periodic discovery; IP=0 uses the sender address. */
    put32(request+88,arc4random()); put32(request+92,MAX_TRACKER_PEERS);
    request[96]=params->port>>8; request[97]=params->port;
    int n=exchange(sock,request,sizeof(request),response,sizeof(response),1);
    if (n<20) goto done;
    result=calloc(1,sizeof(*result)); if (!result) goto done;
    result->interval=counter(response+8); if (result->interval<60) result->interval=60;
    result->incomplete=counter(response+12); result->complete=counter(response+16);
    int count=(n-20)/6; if (count>MAX_TRACKER_PEERS) count=MAX_TRACKER_PEERS;
    if (count) {
        result->peers=calloc((size_t)count,sizeof(*result->peers));
        if (!result->peers) { free(result); result=NULL; goto done; }
        result->num_peers=count;
        for (int i=0;i<count;i++) {
            memcpy(&result->peers[i].ip,response+20+6*i,4);
            memcpy(&result->peers[i].port,response+24+6*i,2);
        }
    }
done:
    net_close(sock); return result;
}
