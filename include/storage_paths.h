#ifndef PS5TORRENT_STORAGE_PATHS_H
#define PS5TORRENT_STORAGE_PATHS_H

#include <stddef.h>

/**
 * Maximum number of storage paths.
 */
#define MAX_STORAGE_PATHS 12

/**
 * A storage path entry.
 */
typedef struct {
    char  path[256];     // Full path
    char  label[64];     // Human-readable label
    char  icon[8];       // Emoji/icon for UI
    int   is_removable;  // 1 = removable media (USB etc.)
    int   is_default;    // 1 = default download location
} storage_path_t;

/**
 * Get the list of all known PS5 storage paths.
 * @param paths    Output array
 * @param count    Output: number of paths
 */
void storage_paths_get(storage_path_t *paths, size_t *count);

/**
 * Get the default download path.
 * @return Pointer to the default path string
 */
const char *storage_paths_get_default(void);

/**
 * Check if a path exists on the filesystem.
 * @param path Path to check
 * @return 1 if exists, 0 if not
 */
int storage_path_exists(const char *path);

/**
 * Check whether an existing storage directory can be written by this process.
 * @param path Directory to check
 * @return 1 if writable and searchable, 0 otherwise
 */
int storage_path_is_writable(const char *path);

#endif /* PS5TORRENT_STORAGE_PATHS_H */
