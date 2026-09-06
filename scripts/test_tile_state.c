#include "tile_state.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_bytes(const char *path, const void *data, size_t length)
{
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(data, 1, length, f) == length); assert(!fclose(f));
}

int main(void)
{
    char root[] = "/tmp/ps5torrent-tile-XXXXXX";
    assert(mkdtemp(root));
    char app[512], meta[512], marker[512], pkg[600], json[600];
    snprintf(app, sizeof(app), "%s/app", root);
    snprintf(meta, sizeof(meta), "%s/appmeta", root);
    snprintf(marker, sizeof(marker), "%s/version", root);
    snprintf(pkg, sizeof(pkg), "%s/app.pkg", app);
    snprintf(json, sizeof(json), "%s/param.json", meta);
    assert(!mkdir(app, 0700)); assert(!mkdir(meta, 0700));
    assert(!tile_state_present(app));
    assert(!tile_state_write_marker(marker, "02.000.004"));
    assert(!tile_state_version(app, meta, marker, "02.000.004"));
    unlink(marker);
    /* Regression: installed package without an unpacked sce_sys directory. */
    write_bytes(pkg, "PKG", 3);
    assert(tile_state_present(app));
    assert(!tile_state_version(app, meta, marker, "02.000.004"));
    const char metadata[] = "{\"contentVersion\":\"02.000.004\"}";
    write_bytes(json, metadata, sizeof(metadata) - 1);
    assert(tile_state_version(app, meta, marker, "02.000.004"));
    assert(!tile_state_version(app, meta, marker, "02.000.005"));
    unlink(json);
    /* Version stored in binary data across the streaming buffer boundary. */
    char bytes[8300] = {0}; memcpy(bytes + 8190, "02.000.004", 10);
    write_bytes(pkg, bytes, sizeof(bytes));
    assert(tile_state_version(app, meta, marker, "02.000.004"));
    write_bytes(pkg, "PKG", 3);
    assert(!tile_state_write_marker(marker, "02.000.004"));
    assert(tile_state_version(app, meta, marker, "02.000.004"));
    unlink(pkg);
    assert(!tile_state_version(app, meta, marker, "02.000.004"));
    unlink(marker); rmdir(app); rmdir(meta); rmdir(root);
    puts("Tile state: packaged installs, appmeta, binary version search, stale markers passed");
}
