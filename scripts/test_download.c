#include "torrent_mgr.h"
#include "app_log.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
int main(int argc, char **argv)
{
    assert(argc==4); alarm(15);
    FILE *f=fopen(argv[1],"rb"); assert(f); fseek(f,0,SEEK_END);
    long len=ftell(f); rewind(f); unsigned char *raw=malloc((size_t)len); assert(raw);
    assert(fread(raw,1,(size_t)len,f)==(size_t)len); fclose(f);
    app_log_open(argv[2]); torrent_mgr_init();
    int id=torrent_mgr_add_raw(raw,(size_t)len,argv[2]); free(raw); assert(id==0);
    assert(torrent_mgr_start(id)==0);
    managed_torrent_t *mt=torrent_mgr_get(id);
    if (!strcmp(argv[3],"disk")) { close(mt->file_writer.fd); mt->file_writer.fd=-1; }
    double deadline=now()+12; int success=0;
    while (now()<deadline) {
        double start=now(); torrent_mgr_tick(); assert(now()-start<0.5);
        char status[4096]; torrent_mgr_status_json(status,sizeof(status));
        if ((!strcmp(argv[3],"good") || !strcmp(argv[3],"choke_resume") ||
             !strcmp(argv[3],"webseed")) &&
            mt->state==TORRENT_DONE) {
            assert(mt->downloaded==mt->total_size); success=1; break;
        }
        if (!strcmp(argv[3],"disk") && mt->state==TORRENT_ERROR) {
            assert(mt->error_type==TERR_WRITE_FAILED && mt->piece_mgr.num_complete==0); success=1; break;
        }
        if (!strcmp(argv[3],"choke") && mt->active_peers > 0 &&
            mt->peers[0].choked && mt->peers[0].piece_received > 0) {
            mt->peers[0].last_activity = 1;
        }
        if ((!strcmp(argv[3],"corrupt") || !strcmp(argv[3],"disconnect") || !strcmp(argv[3],"choke")) &&
             mt->num_peers>0 && mt->active_peers==0) {
            assert(mt->piece_mgr.num_complete==0 && mt->downloaded==0);
            assert(mt->piece_mgr.pieces[1].state==PIECE_FREE); success=1; break;
        }
        usleep(1000);
    }
    if (!success) { size_t n; char *log=app_log_read(&n); if(log){fputs(log,stderr); free(log);} }
    assert(success);
    torrent_mgr_shutdown(); app_log_close(); return 0;
}
