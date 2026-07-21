#!/usr/bin/env bash
# Verifica pelo Mac se o loader ou o painel PS5Torrent estão acessíveis.

set -u

if [ "$#" -ne 1 ]; then
    printf 'Uso: %s IP_DO_PS5\n' "$0" >&2
    exit 2
fi

PS5_ADDRESS="$1"

if ! printf '%s' "$PS5_ADDRESS" | grep -Eq '^[0-9A-Fa-f:.]+$'; then
    printf 'Erro: endereço inválido: %s\n' "$PS5_ADDRESS" >&2
    exit 2
fi

printf 'PS5Torrent em %s\n\n' "$PS5_ADDRESS"

STATUS="$(curl --silent --show-error --connect-timeout 2 --max-time 4 \
    "http://$PS5_ADDRESS:8080/api/status" 2>/dev/null)"
if [ -n "$STATUS" ]; then
    printf '✓ Serviço ativo na porta 8080\n'
    if command -v python3 >/dev/null 2>&1; then
        printf '%s' "$STATUS" | python3 -m json.tool 2>/dev/null || \
            printf '%s\n' "$STATUS"
    else
        printf '%s\n' "$STATUS"
    fi
    printf '\nPainel: http://%s:8080/\n' "$PS5_ADDRESS"
    exit 0
fi

printf '✗ O serviço não respondeu na porta 8080\n'

LOADER_FOUND=0
for PORT in 9021 9020; do
    if nc -z -w 2 "$PS5_ADDRESS" "$PORT" >/dev/null 2>&1; then
        printf '✓ Loader acessível na porta %s\n' "$PORT"
        LOADER_FOUND=1
    else
        printf '✗ Nada respondeu na porta %s\n' "$PORT"
    fi
done

printf '\n'
if [ "$LOADER_FOUND" -eq 1 ]; then
    printf '%s\n' \
        'O loader está disponível, mas o PS5Torrent não está ativo.' \
        'Envie novamente o ELF corrigido e rode este diagnóstico outra vez.'
else
    printf '%s\n' \
        'Nem o painel nem um loader responderam.' \
        'Confirme o IP, a mesma rede Wi-Fi/LAN e deixe o loader aberto no PS5.'
fi

exit 1
