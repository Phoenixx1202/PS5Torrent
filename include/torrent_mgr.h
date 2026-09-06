#ifndef PS5TORRENT_TORRENT_MGR_H
#define PS5TORRENT_TORRENT_MGR_H

#include <stdint.h>
#include <stddef.h>
#include "torrent.h"
#include "piece_mgr.h"
#include "file_io.h"
#include "tracker.h"
#include "peer_wire.h"

/**
 * Maximum concurrent torrents.
 */
#define MAX_TORRENTS 8

/**
 * Maximum peers per torrent.
 */
#define MAX_PEERS_PER_TORRENT 50

/**
 * Torrent state.
 */
typedef enum {
    TORRENT_STOPPED,
    TORRENT_DOWNLOADING,
    TORRENT_SEEDING,
    TORRENT_ERROR,
    TORRENT_DONE
} torrent_state_t;

/**
 * Torrent error types.
 */
typedef enum {
    TERR_NONE,
    TERR_PARSE_FAILED,
    TERR_TRACKER_FAILED,
    TERR_NO_PEERS,
    TERR_WRITE_FAILED,
    TERR_HASH_MISMATCH
} torrent_error_t;

/**
 * A peer connection.
 */
typedef struct {
    int       sock;
    peer_addr_t addr;
    uint8_t   peer_id[20];
    int       choked;
    int       am_interested;
    int       active;
    uint32_t  last_request;
    uint64_t  downloaded;
    uint64_t  uploaded;
    unsigned char *bitfield;
    size_t bitfield_len;
    unsigned char *piece_data;
    size_t piece_size;
    uint32_t piece_received;
    uint32_t pending_length;
    uint64_t request_time;
    uint64_t last_activity;
} torrent_peer_t;

typedef struct {
    peer_addr_t addr;
    unsigned failures;
    uint64_t retry_after;
    int connecting;
} peer_candidate_t;

/**
 * A managed torrent download.
 */
typedef struct {
    char            id[64];          // Unique ID (hash hex)
    uint64_t        generation;      // Invalidates background discovery on stop/remove.
    char            name[256];       // Display name
    char            save_path[512];  // Download directory
    unsigned char   info_hash[20];

    torrent_state_t  state;
    torrent_error_t  error_type;
    char             error_msg[256];

    torrent_t       *torrent;
    piece_mgr_t      piece_mgr;
    file_writer_t    file_writer;

    unsigned char    peer_id[20];
    torrent_peer_t   peers[MAX_PEERS_PER_TORRENT];
    int              num_peers;
    int              active_peers;
    peer_candidate_t peer_candidates[MAX_TRACKER_PEERS];
    int              num_candidates;
    int              next_candidate;

    // Stats
    uint64_t  downloaded;
    uint64_t  uploaded;
    uint64_t  total_size;
    int       tracker_announces;
    int       tracker_interval;
    unsigned  tracker_failures;
    float     progress;
    uint64_t  speed_down;     // bytes/sec
    uint64_t  speed_up;

    // Timing
    uint64_t  last_announce_time;
    uint64_t  last_progress_time;
    uint64_t  last_speed_calc;
    uint64_t  last_downloaded;
    uint64_t  last_uploaded;
    uint64_t  start_time;

    // Source info
    int       is_magnet;      // 1 = from magnet link
    char      magnet_uri[1024]; // Original magnet URI
    char      torrent_source[4096]; // Original .torrent data (for magnet)
} managed_torrent_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the torrent manager.
 */
void torrent_mgr_init(void);

/**
 * Add a torrent from a parsed torrent file.
 * @param torrent  Parsed torrent (takes ownership)
 * @param save_path Download directory
 * @param is_magnet 1 if from magnet link
 * @param magnet_uri Original magnet URI (NULL if not magnet)
 * @return Torrent index, or -1 on error
 */
int torrent_mgr_add(torrent_t *torrent, const char *save_path,
                    int is_magnet, const char *magnet_uri);

/**
 * Add a torrent from raw .torrent data.
 * @param data        .torrent file content
 * @param data_len    Content length
 * @param save_path   Download directory
 * @return Torrent index, or -1 on error
 */
int torrent_mgr_add_raw(const unsigned char *data, size_t data_len,
                        const char *save_path);

/**
 * Parse a magnet URI and start downloading metadata.
 * @param magnet_uri The magnet:?xt=urn:btih:... URI
 * @param save_path  Download directory
 * @return Torrent index, or -1 on error
 */
int torrent_mgr_add_magnet(const char *magnet_uri, const char *save_path);

/**
 * Start downloading a torrent.
 * @param index Torrent index
 * @return 0 on success, -1 on error
 */
int torrent_mgr_start(int index);

/**
 * Stop a torrent.
 */
void torrent_mgr_stop(int index);

/**
 * Remove a torrent and free resources.
 */
void torrent_mgr_remove(int index);

/**
 * Get a torrent by index.
 */
managed_torrent_t *torrent_mgr_get(int index);

/**
 * Get number of active torrents.
 */
int torrent_mgr_count(void);

/**
 * Get total number of torrent slots (including empty).
 */
int torrent_mgr_capacity(void);

/**
 * Find a torrent by info hash.
 * @return Index, or -1 if not found
 */
int torrent_mgr_find(const unsigned char info_hash[20]);

/**
 * Main tick function - call this periodically to process peers.
 * @return Number of active torrents being processed
 */
int torrent_mgr_tick(void);

/**
 * Get JSON status string for all torrents.
 * @param buf    Output buffer
 * @param bufsz  Buffer size
 */
void torrent_mgr_status_json(char *buf, size_t bufsz);

/**
 * Get a human-readable error string.
 */
const char *torrent_mgr_error_str(torrent_error_t err);

/**
 * Shutdown all torrents and free resources.
 */
void torrent_mgr_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PS5TORRENT_TORRENT_MGR_H */
