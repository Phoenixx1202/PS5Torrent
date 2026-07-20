#!/usr/bin/env bash
# Compatibilidade: o projeto só produz payload ELF e bundle de transferência.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/../setup_pkg.sh" "$@"
