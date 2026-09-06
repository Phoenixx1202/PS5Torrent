#!/usr/bin/env bash
# Compatibility entry point for the ELF and embedded media PKG distribution.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/../setup_pkg.sh" "$@"
