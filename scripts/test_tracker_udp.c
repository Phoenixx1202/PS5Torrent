#include "tracker.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
int main(int argc, char **argv)
{
    assert(argc==3);
    unsigned char hash[20], peer[20]; memset(hash,0x12,20); memset(peer,0x34,20);
    tracker_params_t p={ .info_hash=hash, .peer_id=peer, .port=6881,
        .downloaded=0x100000002LL, .left=0x200000003LL, .uploaded=9, .compact=1 };
    tracker_response_t *r=tracker_announce(argv[1],&p);
    if (atoi(argv[2])) {
        assert(r && !r->failure_reason && r->num_peers==1);
        assert(r->interval==120 && r->complete==4 && r->incomplete==2);
        assert(r->peers[0].ip==inet_addr("127.0.0.1") && ntohs(r->peers[0].port)==6881);
    } else assert(!r);
    tracker_response_free(r);
    return 0;
}
