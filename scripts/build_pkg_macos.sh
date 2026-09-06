#!/usr/bin/env bash
# Distribute the same media tile embedded in the ELF.
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python3 "$PROJECT_DIR/scripts/build_media_pkg.py" --check
make -C "$PROJECT_DIR" all
ELF_NAME="PS5Torrent.elf"
if [ "${USE_LIBTORRENT:-0}" = "1" ]; then
    ELF_NAME="PS5Torrent-libtorrent.elf"
fi
mkdir -p "$PROJECT_DIR/dist"
cp "$PROJECT_DIR/pkg/PS5Torrent.pkg" "$PROJECT_DIR/dist/PS5Torrent.pkg"
cp "$PROJECT_DIR/$ELF_NAME" "$PROJECT_DIR/dist/PS5Torrent.elf"
printf 'ELF e PKG de Mídias disponíveis em %s/dist\n' "$PROJECT_DIR"
