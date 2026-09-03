#ifndef PS5TORRENT_PS5_JAILBREAK_H
#define PS5TORRENT_PS5_JAILBREAK_H

/**
 * Ask etaHEN's local command service to jailbreak the current fPKG process.
 * Payloads started by an ELF loader are normally privileged already, so a
 * missing daemon is not fatal and is reported to the caller.
 *
 * @return 0 when etaHEN accepted the request, -1 otherwise.
 */
int ps5_request_jailbreak(void);

#endif /* PS5TORRENT_PS5_JAILBREAK_H */
