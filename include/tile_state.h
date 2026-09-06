#ifndef TILE_STATE_H
#define TILE_STATE_H
/* Check installed title layouts without requiring unpacked sce_sys files. */
int tile_state_present(const char *app_dir);
int tile_state_version(const char *app_dir, const char *appmeta_dir,
                       const char *marker, const char *version);
int tile_state_write_marker(const char *path, const char *version);
#endif
