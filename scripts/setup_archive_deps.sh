#!/usr/bin/env bash
# Build the static archive libraries used by the PS5 payload.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_DIR="${PS5_PAYLOAD_SDK:-$SCRIPT_DIR/.deps/ps5-payload-sdk}"
BUILD_DIR="$SCRIPT_DIR/.deps/archive-build"
PREFIX_DIR="$SDK_DIR/target/user/homebrew"
MARKER="$PREFIX_DIR/lib/.ps5torrent-archive-deps-v1"

if [ ! -f "$SDK_DIR/toolchain/prospero.sh" ]; then
    printf 'Erro: PS5 payload SDK não encontrado em %s\n' "$SDK_DIR" >&2
    exit 1
fi

if [ -f "$MARKER" ] && [ -f "$PREFIX_DIR/lib/libarchive.a" ]; then
    printf 'Dependências de extração já instaladas: %s\n' "$PREFIX_DIR"
    exit 0
fi

for tool in curl tar make shasum; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'Erro: comando necessário não encontrado: %s\n' "$tool" >&2
        exit 1
    fi
done

mkdir -p "$BUILD_DIR/downloads" "$BUILD_DIR/src" "$PREFIX_DIR/include" "$PREFIX_DIR/lib"

download() {
    local url="$1"
    local output="$2"
    local expected="$3"

    if [ ! -f "$output" ]; then
        printf 'Baixando %s...\n' "$(basename "$output")"
        curl --fail --location --retry 3 "$url" --output "$output"
    fi

    local actual
    actual="$(shasum -a 256 "$output" | awk '{print $1}')"
    if [ "$actual" != "$expected" ]; then
        printf 'Erro: checksum inválido para %s\n' "$output" >&2
        printf 'Esperado: %s\nObtido:   %s\n' "$expected" "$actual" >&2
        exit 1
    fi
}

extract_once() {
    local archive="$1"
    local directory="$2"
    if [ ! -d "$directory" ]; then
        tar -xf "$archive" -C "$BUILD_DIR/src"
    fi
}

ZLIB_ARCHIVE="$BUILD_DIR/downloads/zlib-1.3.2.tar.gz"
XZ_ARCHIVE="$BUILD_DIR/downloads/xz-5.4.6.tar.xz"
BZIP2_ARCHIVE="$BUILD_DIR/downloads/bzip2-1.0.8.tar.gz"
LIBARCHIVE_ARCHIVE="$BUILD_DIR/downloads/libarchive-3.7.4.tar.gz"

download \
    "https://zlib.net/zlib-1.3.2.tar.gz" \
    "$ZLIB_ARCHIVE" \
    "bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16"
download \
    "https://github.com/tukaani-project/xz/releases/download/v5.4.6/xz-5.4.6.tar.xz" \
    "$XZ_ARCHIVE" \
    "b92d4e3a438affcf13362a1305cd9d94ed47ddda22e456a42791e630a5644f5c"
download \
    "https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz" \
    "$BZIP2_ARCHIVE" \
    "ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269"
download \
    "https://www.libarchive.org/downloads/libarchive-3.7.4.tar.gz" \
    "$LIBARCHIVE_ARCHIVE" \
    "7875d49596286055b52439ed42f044bd8ad426aa4cc5aabd96bfe7abb971d5e8"

extract_once "$ZLIB_ARCHIVE" "$BUILD_DIR/src/zlib-1.3.2"
extract_once "$XZ_ARCHIVE" "$BUILD_DIR/src/xz-5.4.6"
extract_once "$BZIP2_ARCHIVE" "$BUILD_DIR/src/bzip2-1.0.8"
extract_once "$LIBARCHIVE_ARCHIVE" "$BUILD_DIR/src/libarchive-3.7.4"

# The SDK helper exports the PS5 cross compiler, sysroot and homebrew prefix.
# shellcheck disable=SC1090
source "$SDK_DIR/toolchain/prospero.sh"

if [ ! -f "$PREFIX_DIR/lib/libz.a" ]; then
    printf 'Compilando zlib para PS5...\n'
    (
        cd "$BUILD_DIR/src/zlib-1.3.2"
        make distclean >/dev/null 2>&1 || true
        ./configure --prefix="$PREFIX" --static
        make
        make DESTDIR="$PS5_SYSROOT" install
    )
fi

if [ ! -f "$PREFIX_DIR/lib/liblzma.a" ]; then
    printf 'Compilando liblzma para PS5...\n'
    (
        cd "$BUILD_DIR/src/xz-5.4.6"
        make distclean >/dev/null 2>&1 || true
        ./configure --prefix="$PREFIX" --host=x86_64-pc-freebsd \
            --enable-static --disable-shared --disable-nls \
            --disable-rpath --disable-scripts --disable-doc
        make
        make DESTDIR="$PS5_SYSROOT" install
    )
fi

if [ ! -f "$PREFIX_DIR/lib/libbz2.a" ]; then
    printf 'Compilando bzip2 para PS5...\n'
    (
        cd "$BUILD_DIR/src/bzip2-1.0.8"
        make clean >/dev/null 2>&1 || true
        make libbz2.a CC="$CC" AR="$AR" RANLIB="$RANLIB"
        install -m 644 bzlib.h "$PREFIX_DIR/include/bzlib.h"
        install -m 644 libbz2.a "$PREFIX_DIR/lib/libbz2.a"
    )
fi

if [ ! -f "$PREFIX_DIR/lib/libarchive.a" ]; then
    printf 'Compilando libarchive para PS5...\n'
    (
        cd "$BUILD_DIR/src/libarchive-3.7.4"
        make distclean >/dev/null 2>&1 || true
        ./configure --prefix="$PREFIX" --host=x86_64-pc-freebsd \
            --enable-static --disable-shared \
            --disable-bsdtar --disable-bsdcat --disable-bsdcpio \
            --disable-acl --without-openssl --without-xml2 \
            --without-expat --without-zstd --without-lz4 --without-lzo2
        make
        make DESTDIR="$PS5_SYSROOT" install
    )
fi

touch "$MARKER"
printf 'Dependências de extração instaladas em %s\n' "$PREFIX_DIR"
