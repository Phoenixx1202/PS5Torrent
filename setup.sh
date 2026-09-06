#!/usr/bin/env bash
# Instala o SDK localmente e compila o payload sem alterar /opt ou arquivos do shell.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_VERSION="${PS5_SDK_VERSION:-v0.41}"
SDK_SHA256="ebfb0acb5260511951a80e17db41650c62d20a8caf8659a230b928dc85005984"
SDK_DIR="${PS5_PAYLOAD_SDK:-$SCRIPT_DIR/.deps/ps5-payload-sdk}"
SDK_URL="https://github.com/ps5-payload-dev/sdk/releases/download/${SDK_VERSION}/ps5-payload-sdk.zip"
INSTALL_DEPS=1
BUILD_PROJECT=1

usage() {
    printf '%s\n' \
        "Uso: ./setup.sh [--no-deps] [--sdk-only]" \
        "" \
        "  --no-deps   Não instala dependências pelo Homebrew" \
        "  --sdk-only  Instala/verifica o SDK sem compilar o projeto"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --no-deps) INSTALL_DEPS=0 ;;
        --sdk-only) BUILD_PROJECT=0 ;;
        --help|-h) usage; exit 0 ;;
        *) printf 'Opção desconhecida: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

command_exists() {
    command -v "$1" >/dev/null 2>&1
}

install_macos_dependencies() {
    if ! command_exists brew; then
        printf '%s\n' \
            "Erro: Homebrew não encontrado." \
            "Instale em https://brew.sh e execute este script novamente." >&2
        exit 1
    fi

    for formula in llvm@18 socat; do
        if ! brew list --versions "$formula" >/dev/null 2>&1; then
            brew install "$formula"
        fi
    done
}

configure_llvm() {
    case "$(uname -s)" in
        Darwin)
            if ! command_exists brew; then
                printf 'Erro: Homebrew é necessário para localizar o LLVM 18.\n' >&2
                exit 1
            fi
            LLVM_PREFIX="$(brew --prefix llvm@18 2>/dev/null || true)"
            if [ -z "$LLVM_PREFIX" ] || [ ! -x "$LLVM_PREFIX/bin/llvm-config" ]; then
                printf 'Erro: LLVM 18 não encontrado. Execute: brew install llvm@18\n' >&2
                exit 1
            fi
            export LLVM_CONFIG="$LLVM_PREFIX/bin/llvm-config"
            ;;
        Linux)
            if command_exists llvm-config-18; then
                export LLVM_CONFIG="$(command -v llvm-config-18)"
            elif command_exists llvm-config; then
                export LLVM_CONFIG="$(command -v llvm-config)"
            else
                printf 'Erro: llvm-config não encontrado. Instale clang-18 e lld-18.\n' >&2
                exit 1
            fi
            ;;
        *)
            printf 'Erro: sistema não suportado: %s\n' "$(uname -s)" >&2
            exit 1
            ;;
    esac

    printf 'LLVM: %s (%s)\n' "$LLVM_CONFIG" "$($LLVM_CONFIG --version)"
}

install_sdk() {
    if [ -f "$SDK_DIR/toolchain/prospero.mk" ]; then
        printf 'SDK já instalado: %s\n' "$SDK_DIR"
        return
    fi

    for tool in curl unzip shasum; do
        if ! command_exists "$tool"; then
            printf 'Erro: comando necessário não encontrado: %s\n' "$tool" >&2
            exit 1
        fi
    done

    DEPS_DIR="$SCRIPT_DIR/.deps"
    ARCHIVE="$DEPS_DIR/ps5-payload-sdk-${SDK_VERSION}.zip"
    EXTRACT_DIR="$DEPS_DIR/.sdk-extract-${SDK_VERSION}"
    mkdir -p "$DEPS_DIR"

    printf 'Baixando ps5-payload-sdk %s...\n' "$SDK_VERSION"
    curl --fail --location --retry 3 "$SDK_URL" --output "$ARCHIVE"

    ACTUAL_SHA256="$(shasum -a 256 "$ARCHIVE" | awk '{print $1}')"
    if [ "$ACTUAL_SHA256" != "$SDK_SHA256" ]; then
        printf 'Erro: checksum inválido para %s\n' "$ARCHIVE" >&2
        printf 'Esperado: %s\nObtido:   %s\n' "$SDK_SHA256" "$ACTUAL_SHA256" >&2
        exit 1
    fi

    rm -rf "$EXTRACT_DIR"
    mkdir -p "$EXTRACT_DIR"
    unzip -q "$ARCHIVE" -d "$EXTRACT_DIR"

    if [ ! -f "$EXTRACT_DIR/ps5-payload-sdk/toolchain/prospero.mk" ]; then
        printf 'Erro: o arquivo baixado não contém um SDK válido.\n' >&2
        exit 1
    fi

    rm -rf "$SDK_DIR"
    mv "$EXTRACT_DIR/ps5-payload-sdk" "$SDK_DIR"
    rmdir "$EXTRACT_DIR"
    printf 'SDK instalado: %s\n' "$SDK_DIR"
}

printf 'Preparando PS5Torrent em %s\n' "$SCRIPT_DIR"

if [ "$(uname -s)" = "Darwin" ] && [ "$INSTALL_DEPS" -eq 1 ]; then
    install_macos_dependencies
fi

configure_llvm
install_sdk

export PS5_PAYLOAD_SDK="$SDK_DIR"

if [ "$BUILD_PROJECT" -eq 1 ]; then
    make -C "$SCRIPT_DIR" clean all
    printf '\nBuild concluído: %s/PS5Torrent.elf\n' "$SCRIPT_DIR"
else
    printf '\nSDK pronto. Para compilar: make\n'
fi
