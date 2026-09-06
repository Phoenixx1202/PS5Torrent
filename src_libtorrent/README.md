# PS5Torrent libtorrent lab

This directory contains the first PS5Torrent-specific proof for the planned
libtorrent migration. It builds a separate ELF and does not replace the current
C engine.

The lab target uses:

- libtorrent-rasterbar `2.0.12`;
- Boost `1.84.0`;
- C++17 through the PS5 Payload SDK;
- DHT, LSD, PEX, uTP, OpenSSL and protocol encryption;
- `posix_disk_io` with `always_pwrite` for predictable large-file writes.

Build with:

```sh
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk \
LLVM_CONFIG=/usr/bin/llvm-config-18 \
make LIBTORRENT_DEPS_DIR=/tmp/ps5torrent-libtorrent \
     LIBTORRENT_BUILD_DIR=/tmp/ps5torrent-libtorrent-build \
     libtorrent-lab
```

The ELF is copied to:

```text
dist/ps5torrent-libtorrent-lab.elf
```

On the PS5, the first argument may be either a `magnet:?` URI or a path to a
`.torrent` file. Without arguments it reads:

```text
/data/PS5Torrent/input.torrent
```

It writes downloaded data and state under:

```text
/data/PS5Torrent/libtorrent-downloads
/data/PS5Torrent/libtorrent-state
/data/PS5Torrent/PS5Torrent-libtorrent-lab.log
```

The next integration step is replacing this single-source lab loop with a small
adapter that exposes add, pause, resume, remove, status and shutdown operations
to the existing PS5Torrent HTTP/UI layer.
