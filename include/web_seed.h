#ifndef PS5TORRENT_WEB_SEED_H
#define PS5TORRENT_WEB_SEED_H

#include "torrent.h"
#include <stddef.h>

int web_seed_fetch_piece(const torrent_t *torrent, size_t piece_index,
                         unsigned char *buffer, size_t length);
int web_seed_fetch_pieces(const torrent_t *torrent, size_t first_piece,
                          size_t piece_count, const size_t *piece_sizes,
                          unsigned char *buffer);

#endif
