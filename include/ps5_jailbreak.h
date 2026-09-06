#ifndef PS5TORRENT_PS5_JAILBREAK_H
#define PS5TORRENT_PS5_JAILBREAK_H

/**
 * Configure the current process credentials and root directory directly
 * through ps5-payload-sdk, following Spectrum's own-process setup.
 *
 * @return 0 on success, -1 if any required operation failed.
 */
int ps5_request_jailbreak(void);

#endif /* PS5TORRENT_PS5_JAILBREAK_H */
