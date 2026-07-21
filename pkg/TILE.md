# Aplicativo PS5Torrent na tela inicial

O pacote inclui o próprio `ps5_torrent.elf` como `eboot.bin`. Ao abrir o ícone
na tela inicial, ele inicia o serviço de torrent, mostra uma notificação com o
endereço de rede e abre `http://127.0.0.1:8080/` no navegador do console. Não é
necessário enviar o payload novamente a cada uso.

Arquivos prontos neste diretório:

- `sce_sys/param.json`: nome, Content ID e URL local;
- `sce_sys/icon0.png`: ícone 512 × 512 da tela inicial;
- `sce_sys/pic0.png` e `pic1.png`: arte de fundo.

## Gerar o fPKG no macOS

Pré-requisitos:

- .NET 10 (`brew install dotnet`);
- dependências de compilação já instaladas por `./setup.sh`.

Na raiz do projeto, execute:

```bash
./scripts/build_pkg_macos.sh
```

O script recompila o ELF, atualiza as artes e usa a
[LibProsperoPKG](https://github.com/SvenGDK/LibProsperoPKG) v2.5 para gerar o
arquivo em `dist/`. A dependência fica isolada em `.deps/` e sua revisão é
validada antes do build.

Instale o fPKG resultante pelo instalador de pacotes do ambiente homebrew do
console. O pacote é experimental e requer um PS5 em modo compatível com fPKG;
a aceitação e a inicialização ainda precisam ser confirmadas no aparelho.

O Content ID é exclusivo do projeto e o aplicativo abre somente o endereço
local do próprio console. Para atualizar a arte, edite `generate_icon.py` e
gere o pacote novamente.
