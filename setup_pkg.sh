#!/usr/bin/env bash
# Cria um bundle para transferência manual. Não gera um PKG instalável.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAGING_DIR="$SCRIPT_DIR/pkg/staging"
ARCHIVE="$SCRIPT_DIR/pkg/ps5-torrent-bundle.tar.gz"
DEPLOY_IP=""

usage() {
    printf '%s\n' \
        "Uso: ./setup_pkg.sh [--deploy IP_DO_PS5]" \
        "" \
        "Cria um bundle tar.gz para inspeção/transferência manual." \
        "Este script não gera um arquivo .pkg instalável."
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --deploy)
            if [ "$#" -lt 2 ]; then
                printf 'Erro: --deploy requer o IP do PS5.\n' >&2
                exit 2
            fi
            DEPLOY_IP="$2"
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            printf 'Opção desconhecida: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

printf '%s\n' \
    "PS5Torrent - bundle experimental" \
    "Aviso: a saída não é um PKG PS5 instalável."

make -C "$SCRIPT_DIR" clean all

if command -v python3 >/dev/null 2>&1; then
    if ! python3 "$SCRIPT_DIR/pkg/generate_icon.py"; then
        printf 'Aviso: ícones não foram gerados (instale Pillow se necessário).\n' >&2
    fi
fi

rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR/sce_sys"
cp "$SCRIPT_DIR/ps5_torrent.elf" "$STAGING_DIR/eboot.bin"

for asset in "$SCRIPT_DIR"/pkg/sce_sys/*.json "$SCRIPT_DIR"/pkg/sce_sys/*.png; do
    if [ -f "$asset" ]; then
        cp "$asset" "$STAGING_DIR/sce_sys/"
    fi
done

tar -C "$STAGING_DIR" -czf "$ARCHIVE" .
printf 'Bundle criado: %s\n' "$ARCHIVE"

if [ -n "$DEPLOY_IP" ]; then
    printf 'Enviando ELF para %s:9021...\n' "$DEPLOY_IP"
    PS5_HOST="$DEPLOY_IP" PS5_PORT=9021 make -C "$SCRIPT_DIR" test
fi
