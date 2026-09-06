# PS5Torrent em Mídias

O `PS5Torrent.elf` v2.0.4 incorpora `PS5Torrent.pkg` (contentVersion
`02.000.004`). Ao iniciar, prepara a rede e o servidor HTTP e instala o pacote
quando necessário. Após confirmar a instalação, mostra a mensagem de que o
PS5Torrent está pronto para ser aberto na aba Mídias. Não abre o título nem o
navegador automaticamente. Falhas de instalação têm uma mensagem separada.

Como no Spectrum, a thread principal recebe o nome `PS5Torrent.elf`, igual ao
nome do arquivo, para a identificação do payload ativo no Payload Manager.

O pacote segue o modelo de deeplink do Spectrum Library: categoria de Mídias
65536 e URL `http://127.0.0.1:12389/`. O ELF precisa continuar ativo; o ícone
não inicia o serviço sozinho depois de reiniciar o console.

- `sce_sys/icon0.png`: ícone fornecido, 512 × 512.
- `sce_sys/pic0.png` e `pic1.png`: fundo fornecido, 1920 × 1080.
- `sce_sys/param.json`: metadados do título de Mídias.
- `eboot.bin`: vazio, como no tile de referência.
- `media-package.sha256.json`: integridade do pacote e suas fontes.

O pacote pronto fica versionado para compilar o ELF em Linux/macOS sem a
ferramenta de publicação. Para recriar o PKG depois de alterar os assets:

```powershell
python scripts/build_media_pkg.py
python scripts/build_media_pkg.py --check
```

O script procura `prospero-pub-cmd.exe` no PATH ou no local padrão do Windows.
É possível informar outro caminho pela variável `PUB_CMD`. Depois recompile
o ELF com `make`. `scripts/build_pkg_macos.sh` copia os dois artefatos para
`dist/`, verificando antes a integridade do PKG.

A instalação usa AppInstUtil e reconhece títulos em `app.pkg`, metadados de
`sce_sys`/`appmeta` e a versão local, como o Spectrum. A versão local sozinha
não conta como instalação presente. O pacote permanece em
`/data/PS5Torrent/PS5Torrent.pkg`. Se a solicitação foi aceita mas o registro
 ainda não apareceu, o estado é pendente; o serviço continua
ativo e verifica o registro a cada segundo, sem emitir um falso erro.
O acesso ao sistema é preparado diretamente pelo SDK, sem daemon externo.
A aceitação pelo
instalador, exibição em Mídias e abertura precisam ser validadas no PS5 alvo.

O `img_verify` comercial identifica a categoria Mídias, mas rejeita este
formato homebrew: entre os erros estão o `eboot.bin` vazio, o deeplink,
metadados de publicação ausentes e as artes em 1920 × 1080. Esses elementos
seguem a referência solicitada; o PKG não foi aprovado por esse verificador.
