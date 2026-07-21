#!/usr/bin/env bash
# Gera um fPKG de homebrew PS5 diretamente no macOS usando LibProsperoPKG.

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIB_DIR="$PROJECT_DIR/.deps/LibProsperoPKG"
LIB_REPO="https://github.com/SvenGDK/LibProsperoPKG.git"
LIB_TAG="v2.5"
LIB_COMMIT="3e159f2ed1d2c2c53c003f79ace378d987773fe3"
MAC_SHA3_SOURCE="$PROJECT_DIR/tools/ps5_pkg_builder/ManagedSha3.cs"
MAC_SHA3_PATCH="$PROJECT_DIR/tools/ps5_pkg_builder/libprosperopkg-macos-sha3.patch"
STAGE_DIR="$PROJECT_DIR/.build/ps5torrent-pkg-root"
OUTPUT_DIR="$PROJECT_DIR/dist"
DOTNET_CLI_HOME="$PROJECT_DIR/.build/dotnet-home"
NUGET_PACKAGES="$PROJECT_DIR/.build/nuget-packages"

export DOTNET_CLI_HOME NUGET_PACKAGES
export DOTNET_NOLOGO=1
export DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
export DOTNET_CLI_TELEMETRY_OPTOUT=1

if ! command -v dotnet >/dev/null 2>&1; then
    printf '%s\n' \
        "Erro: .NET 10 não encontrado." \
        "Instale no Mac com: brew install dotnet" >&2
    exit 1
fi

DOTNET_MAJOR="$(dotnet --version | cut -d. -f1)"
if [ "$DOTNET_MAJOR" -lt 10 ]; then
    printf 'Erro: é necessário .NET 10 ou mais recente. Encontrado: %s\n' \
        "$(dotnet --version)" >&2
    exit 1
fi

if [ ! -d "$LIB_DIR/.git" ]; then
    mkdir -p "$PROJECT_DIR/.deps"
    git clone --depth 1 --branch "$LIB_TAG" "$LIB_REPO" "$LIB_DIR"
fi

ACTUAL_COMMIT="$(git -C "$LIB_DIR" rev-parse HEAD)"
if [ "$ACTUAL_COMMIT" != "$LIB_COMMIT" ]; then
    printf '%s\n' \
        "Erro: versão inesperada da LibProsperoPKG em $LIB_DIR" \
        "Esperado: $LIB_COMMIT" \
        "Obtido:   $ACTUAL_COMMIT" >&2
    exit 1
fi

# The macOS cryptography provider does not expose SHA3_256.IsSupported to .NET.
# Add the portable implementation and redirect the four library entry points.
cp "$MAC_SHA3_SOURCE" "$LIB_DIR/src/LibProsperoPkg/Util/ManagedSha3.cs"
if git -C "$LIB_DIR" apply --check "$MAC_SHA3_PATCH" 2>/dev/null; then
    git -C "$LIB_DIR" apply "$MAC_SHA3_PATCH"
elif ! git -C "$LIB_DIR" apply --reverse --check "$MAC_SHA3_PATCH" 2>/dev/null; then
    printf 'Erro: não foi possível aplicar a compatibilidade SHA3 para macOS.\n' >&2
    exit 1
fi

make -C "$PROJECT_DIR" all
python3 "$PROJECT_DIR/pkg/generate_icon.py"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/sce_sys" "$OUTPUT_DIR" "$DOTNET_CLI_HOME" "$NUGET_PACKAGES"
cp "$PROJECT_DIR/ps5_torrent.elf" "$STAGE_DIR/eboot.bin"
cp "$PROJECT_DIR/pkg/sce_sys/param.json" "$STAGE_DIR/sce_sys/param.json"
cp "$PROJECT_DIR/pkg/sce_sys/icon0.png" "$STAGE_DIR/sce_sys/icon0.png"
cp "$PROJECT_DIR/pkg/sce_sys/pic0.png" "$STAGE_DIR/sce_sys/pic0.png"
cp "$PROJECT_DIR/pkg/sce_sys/pic1.png" "$STAGE_DIR/sce_sys/pic1.png"

dotnet run --configuration Release \
    --project "$PROJECT_DIR/tools/ps5_pkg_builder/PS5TorrentPkgBuilder.csproj" \
    -- "$STAGE_DIR" "$OUTPUT_DIR"

printf '\nPKG gerado em: %s\n' "$OUTPUT_DIR"
