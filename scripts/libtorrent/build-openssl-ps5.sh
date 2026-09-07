#!/usr/bin/env bash
# Build the static OpenSSL libraries used by the libtorrent PS5 payload.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SDK_DIR="${PS5_PAYLOAD_SDK:-$SCRIPT_DIR/.deps/ps5-payload-sdk}"
BUILD_DIR="${PS5TORRENT_OPENSSL_BUILD_DIR:-$SCRIPT_DIR/.deps/openssl-build}"
OPENSSL_VERSION="${PS5TORRENT_OPENSSL_VERSION:-3.5.8}"
OPENSSL_SHA256="${PS5TORRENT_OPENSSL_SHA256:-a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2}"
PREFIX_DIR="$SDK_DIR/target/user/homebrew"
MARKER="$PREFIX_DIR/lib/.ps5torrent-openssl-$OPENSSL_VERSION"
JOBS="${PS5TORRENT_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"

if [ ! -f "$SDK_DIR/toolchain/prospero.sh" ]; then
    printf 'Error: PS5 payload SDK not found at %s\n' "$SDK_DIR" >&2
    exit 1
fi

if [ -f "$MARKER" ] &&
   [ -f "$PREFIX_DIR/include/openssl/opensslv.h" ] &&
   [ -f "$PREFIX_DIR/lib/libssl.a" ] &&
   [ -f "$PREFIX_DIR/lib/libcrypto.a" ]; then
    printf 'OpenSSL for PS5 already installed: %s\n' "$PREFIX_DIR"
    exit 0
fi

for tool in curl tar make perl; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'Error: required command not found: %s\n' "$tool" >&2
        exit 1
    fi
done

if command -v sha256sum >/dev/null 2>&1; then
    hash_file() { sha256sum "$1" | awk '{print $1}'; }
elif command -v shasum >/dev/null 2>&1; then
    hash_file() { shasum -a 256 "$1" | awk '{print $1}'; }
else
    printf 'Error: sha256sum or shasum is required\n' >&2
    exit 1
fi

mkdir -p "$BUILD_DIR/downloads" "$BUILD_DIR/src"

INSTALL_WITH_SUDO=0
if [ -e "$PREFIX_DIR" ]; then
    if [ ! -w "$PREFIX_DIR" ]; then
        INSTALL_WITH_SUDO=1
    fi
else
    PREFIX_PARENT="$(dirname "$PREFIX_DIR")"
    if [ ! -w "$PREFIX_PARENT" ]; then
        INSTALL_WITH_SUDO=1
    fi
fi

run_install_command() {
    if [ "$INSTALL_WITH_SUDO" -eq 1 ]; then
        if ! command -v sudo >/dev/null 2>&1 || ! sudo -n true >/dev/null 2>&1; then
            printf 'Error: %s is not writable and passwordless sudo is not available\n' "$PREFIX_DIR" >&2
            exit 1
        fi
        sudo -n "$@"
    else
        "$@"
    fi
}

run_install_command mkdir -p "$PREFIX_DIR/include" "$PREFIX_DIR/lib"

ARCHIVE="$BUILD_DIR/downloads/openssl-$OPENSSL_VERSION.tar.gz"
SRC_DIR="$BUILD_DIR/src/openssl-$OPENSSL_VERSION"

if [ ! -f "$ARCHIVE" ]; then
    printf 'Downloading OpenSSL %s...\n' "$OPENSSL_VERSION"
    curl --fail --location --retry 3 \
        "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz" \
        --output "$ARCHIVE"
fi

ACTUAL_SHA256="$(hash_file "$ARCHIVE")"
if [ "$ACTUAL_SHA256" != "$OPENSSL_SHA256" ]; then
    printf 'Error: invalid checksum for %s\n' "$ARCHIVE" >&2
    printf 'Expected: %s\nActual:   %s\n' "$OPENSSL_SHA256" "$ACTUAL_SHA256" >&2
    exit 1
fi

if [ ! -d "$SRC_DIR" ]; then
    tar -xf "$ARCHIVE" -C "$BUILD_DIR/src"
fi

# The SDK helper exports the PS5 cross compiler, sysroot and homebrew prefix.
# shellcheck disable=SC1090
source "$SDK_DIR/toolchain/prospero.sh"

printf 'Building OpenSSL %s for PS5...\n' "$OPENSSL_VERSION"
(
    cd "$SRC_DIR"
    make clean >/dev/null 2>&1 || true
    ./Configure BSD-x86_64 \
        --prefix="$PREFIX" \
        --openssldir="$PREFIX/ssl" \
        no-shared \
        no-tests \
        no-apps \
        no-docs \
        no-module \
        no-dso \
        no-asm \
        no-ui-console
    make -j"$JOBS"
    run_install_command make DESTDIR="$PS5_SYSROOT" install_sw
)

if [ ! -f "$PREFIX_DIR/include/openssl/opensslv.h" ] ||
   [ ! -f "$PREFIX_DIR/lib/libssl.a" ] ||
   [ ! -f "$PREFIX_DIR/lib/libcrypto.a" ]; then
    printf 'Error: OpenSSL install did not produce the expected PS5 headers and static libraries\n' >&2
    exit 1
fi

run_install_command touch "$MARKER"
printf 'OpenSSL for PS5 installed in %s\n' "$PREFIX_DIR"
