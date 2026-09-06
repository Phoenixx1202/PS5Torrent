#ifndef PS5TORRENT_PS5_BROWSER_H
#define PS5TORRENT_PS5_BROWSER_H

/** Initialize UserService, required by the PS5 browser API. */
int ps5_browser_init(void);
int ps5_browser_open_tile(void);

/**
 * Open the embedded web interface in the PS5 system browser.
 *
 * @param url Local URL served by this payload.
 * @return the PS5 system-service result, or -1 for invalid state.
 */
int ps5_browser_open(const char *url);

/** Terminate UserService if it was initialized. */
void ps5_browser_shutdown(void);

#endif /* PS5TORRENT_PS5_BROWSER_H */
