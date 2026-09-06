#!/usr/bin/env bash
# Prepare the ELF and its embedded media package for distribution.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPLOY_IP=""
if [ "$#" -gt 0 ]; then
    case "$1" in
        --deploy)
            if [ "$#" -ne 2 ]; then
                printf 'Uso: ./setup_pkg.sh [--deploy IP_DO_PS5]\n' >&2
                exit 2
            fi
            DEPLOY_IP="$2"
            ;;
        --help|-h)
            printf 'Uso: ./setup_pkg.sh [--deploy IP_DO_PS5]\nGera dist/PS5Torrent.elf e dist/PS5Torrent.pkg.\n'
            exit 0
            ;;
        *) printf 'Opção desconhecida: %s\n' "$1" >&2; exit 2 ;;
    esac
fi
bash "$SCRIPT_DIR/scripts/build_pkg_macos.sh"
if [ -n "$DEPLOY_IP" ]; then
    PS5_HOST="$DEPLOY_IP" PS5_PORT=9021 make -C "$SCRIPT_DIR" test
fi
