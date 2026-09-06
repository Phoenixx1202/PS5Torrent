/* Media deeplink package installation, following Spectrum Library's flow. */
#include "ps5_tile.h"
#include "ui.h"
#include "tile_state.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ps5/kernel.h>

#define TITLE_ID "HBTR00001"
#define TILE_VERSION "02.000.004"
#define WORK_DIR "/data/PS5Torrent"
#define PACKAGE WORK_DIR "/PS5Torrent.pkg"
#define APP_DIR "/user/app/" TITLE_ID
#define SYSTEM_APP_DIR "/system_ex/app/" TITLE_ID
#define APPMETA_DIR "/system_data/priv/appmeta/" TITLE_ID
#define VERSION_MARKER WORK_DIR "/installed-version"
static int install_pending = 0;

#define ASSET(name, path) \
    __asm__(".section .rodata\n.balign 16\n.global " #name "\n" \
            #name ":\n.incbin \"" path "\"\n.global " #name "_end\n" \
            #name "_end:\n.previous\n"); \
    extern const unsigned char name[], name##_end[]

ASSET(tile_pkg, "pkg/PS5Torrent.pkg");

extern int sceAppInstUtilInitialize(void);
extern int sceAppInstUtilTerminate(void);
typedef struct { char content_id[48]; int type; int platform; } pkg_info_t;
extern int sceAppInstUtilAppInstallPkg(const char *, pkg_info_t *);

static int current_tile(void)
{
    return tile_state_version(APP_DIR, APPMETA_DIR, VERSION_MARKER, TILE_VERSION) ||
           tile_state_version(SYSTEM_APP_DIR, APPMETA_DIR, VERSION_MARKER, TILE_VERSION);
}

int ps5_tile_poll(void)
{
    if (!install_pending) return 0;
    /* A packaged install exposes app.pkg, not necessarily an unpacked param.json. */
    if (!tile_state_present(APP_DIR) && !tile_state_present(SYSTEM_APP_DIR)) return 1;
    if (tile_state_write_marker(VERSION_MARKER, TILE_VERSION))
        ui_error("Could not cache installed media tile version");
    install_pending = 0;
    ui_log("PS5Torrent media tile registered and ready");
    return 0;
}

int ps5_tile_install(void)
{
    if (current_tile()) return 0;
    /* AppInstUtil requires the same privileged auth ID used by Spectrum. */
    uint64_t previous_auth = kernel_get_ucred_authid(-1);
    kernel_set_ucred_authid(-1, 0x4801000000000013ULL);
    int result = sceAppInstUtilInitialize();
    int initialized = result == 0;
    if (result) {
        ui_error("sceAppInstUtilInitialize failed: 0x%x", result);
        result = -1;
        goto done;
    }
    if (mkdir(WORK_DIR, 0755) && errno != EEXIST) { result = -1; goto done; }
    FILE *file = fopen(PACKAGE ".tmp", "wb");
    if (!file) { result = -1; goto done; }
    size_t length = (size_t)(tile_pkg_end - tile_pkg);
    int written = fwrite(tile_pkg, 1, length, file) == length;
    int closed = fclose(file) == 0;
    if (!written || !closed || rename(PACKAGE ".tmp", PACKAGE)) {
        unlink(PACKAGE ".tmp"); result = -1; goto done;
    }
    pkg_info_t info = {0};
    result = sceAppInstUtilAppInstallPkg("/user/data/PS5Torrent/PS5Torrent.pkg", &info);
    if (result) {
        ui_error("sceAppInstUtilAppInstallPkg failed: 0x%x", result);
        result = -1;
        goto done;
    }
    if (!result) {
        ui_log("AppInstUtil accepted PS5Torrent.pkg");
        install_pending = 1;
        /* Let main serve HTTP while registration completes. Sleeping here
         * used to leave stale browser connections queued for 30 seconds. */
        ps5_tile_poll();
        result = install_pending ? 1 : 0;
    }
done:
    if (initialized) sceAppInstUtilTerminate();
    kernel_set_ucred_authid(-1, previous_auth);
    if (result < 0) ui_error("PS5Torrent media PKG installation: 0x%x", result);
    else if (result == 1) ui_log("Media installation accepted; registration is still pending");
    else ui_log("PS5Torrent media tile installed and ready");
    return result;
}
