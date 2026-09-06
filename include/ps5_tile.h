#ifndef PS5_TILE_H
#define PS5_TILE_H
/* 0: installed, 1: accepted and still pending, negative: installation failed. */
int ps5_tile_install(void);
/* Poll an accepted installation without issuing another install request. */
int ps5_tile_poll(void);
#endif
