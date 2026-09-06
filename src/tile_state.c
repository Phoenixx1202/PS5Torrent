#include "tile_state.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int has_file(const char *directory, const char *name)
{
    char path[1024]; struct stat st;
    int n = snprintf(path, sizeof(path), "%s/%s", directory, name);
    return n > 0 && (size_t)n < sizeof(path) && stat(path, &st) == 0 &&
           S_ISREG(st.st_mode) && st.st_size > 0;
}

int tile_state_present(const char *app_dir)
{
    return has_file(app_dir, "app.pkg") || has_file(app_dir, "sce_sys/param.json");
}

/* Streaming binary search: SFO/PKG can contain NUL bytes and large headers. */
static int contains_version(const char *path, const char *version)
{
    unsigned char buffer[8192];
    size_t length = strlen(version), carry = 0, n;
    if (!length || length >= sizeof(buffer)) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    while ((n = fread(buffer + carry, 1, sizeof(buffer) - carry, file)) > 0) {
        size_t total = carry + n;
        for (size_t i = 0; i + length <= total; i++) {
            if (!memcmp(buffer + i, version, length)) { fclose(file); return 1; }
        }
        carry = total < length - 1 ? total : length - 1;
        memmove(buffer, buffer + total - carry, carry);
    }
    fclose(file);
    return 0;
}

int tile_state_version(const char *app_dir, const char *appmeta_dir,
                       const char *marker, const char *version)
{
    const char *dirs[] = {app_dir, app_dir, appmeta_dir, appmeta_dir, app_dir};
    const char *names[] = {"sce_sys/param.json", "sce_sys/param.sfo",
                          "param.json", "param.sfo", "app.pkg"};
    if (!tile_state_present(app_dir)) return 0; /* A marker alone is not installed. */
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        char path[1024];
        int n = snprintf(path, sizeof(path), "%s/%s", dirs[i], names[i]);
        if (n > 0 && (size_t)n < sizeof(path) && contains_version(path, version)) return 1;
    }
    /* Written only after AppInstUtil accepted the package and the title appeared. */
    return contains_version(marker, version);
}

int tile_state_write_marker(const char *path, const char *version)
{
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    size_t length = strlen(version);
    int ok = fwrite(version, 1, length, file) == length;
    return fclose(file) == 0 && ok ? 0 : -1;
}
