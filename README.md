# PS5Torrent

Cliente BitTorrent experimental em C para PS5 com jailbreak, compilado como payload ELF com o [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk).

O projeto expõe uma interface web em `http://IP_DO_PS5:8080/` e recebe arquivos `.torrent` para download no armazenamento escolhido.

> Este software é destinado a pesquisa e homebrew. Use apenas conteúdo que você tem autorização para baixar. Homebrew pode violar os termos da Sony e expor o console a riscos; não use uma conta PSN importante durante testes.

## Estado atual

Já disponível:

- build reproduzível com ps5-payload-sdk v0.41;
- suporte a macOS Apple Silicon e Intel via Homebrew;
- leitura de metainfo `.torrent` single-file e multi-file;
- trackers HTTP, BitTorrent peer wire e verificação SHA-1 das peças;
- escrita em caminhos de armazenamento do PS5;
- servidor e painel web local;
- deploy pelo `elfldr` na porta 9021;
- build automático no GitHub Actions.

Limitações conhecidas:

- magnet links são reconhecidos, mas o download de metadados BEP-9 ainda não foi implementado; use `.torrent`;
- trackers HTTPS e UDP, DHT, PEX, MSE/PE e retomada de download ainda não são suportados;
- a interface web não possui autenticação e deve ser usada somente em rede local confiável;
- o payload compila, mas ainda precisa ser validado em hardware PS5 real;
- os arquivos em `pkg/` criam apenas um bundle experimental para transferência. Eles não produzem um `.pkg` PS5 válido e instalável.

## Instalação no macOS

Pré-requisitos:

- macOS com Command Line Tools (`xcode-select --install`);
- [Homebrew](https://brew.sh).

Na raiz do projeto, execute:

```bash
./setup.sh
```

O script:

1. verifica/instala `llvm@18` e `socat` pelo Homebrew;
2. baixa o ps5-payload-sdk v0.41 oficial;
3. valida o SHA-256 do ZIP;
4. instala o SDK somente em `.deps/ps5-payload-sdk`;
5. faz um build limpo e gera `ps5_torrent.elf`.

Não é necessário usar `sudo`, instalar em `/opt` ou alterar `.zshrc`. O SDK local e os artefatos de build são ignorados pelo Git.

Opções úteis:

```bash
./setup.sh --no-deps   # não executa brew install
./setup.sh --sdk-only  # instala o SDK sem compilar
```

Para usar outra instalação do SDK:

```bash
export PS5_PAYLOAD_SDK=/caminho/para/ps5-payload-sdk
export LLVM_CONFIG="$(brew --prefix llvm@18)/bin/llvm-config"
make clean all
```

## Compilar novamente

Depois da primeira execução de `setup.sh`:

```bash
make
```

Limpeza completa dos artefatos:

```bash
make distclean
```

## Enviar ao PS5

Com um loader ELF ouvindo na porta 9021:

```bash
export PS5_HOST=192.168.1.100
export PS5_PORT=9021
make test
```

Depois, abra no navegador de outro dispositivo da mesma rede:

```text
http://192.168.1.100:8080/
```

Troque o endereço pelo IP mostrado pelo payload. O console precisa permanecer ligado e executando o payload.

## Publicar no GitHub

O repositório já inclui um workflow que recompila o ELF em cada push. O SDK e o ELF local não serão enviados.

```bash
git init
git add .
git commit -m "Initial PS5Torrent release"
gh auth login
gh repo create ps5Torrent --public --source=. --remote=origin --push
```

Se preferir criar o repositório pelo site do GitHub, crie-o vazio e siga as instruções de `git remote add origin` e `git push` exibidas na página.

## Estrutura

```text
.
├── .github/workflows/build.yml  # CI do GitHub
├── include/                     # headers C
├── src/                         # implementação do cliente e servidor web
├── pkg/                         # bundle experimental, não um PKG instalável
├── Makefile                     # build com toolchain atual do SDK
└── setup.sh                     # instalação local reproduzível
```

## Licença

GPL-3.0. Consulte [LICENSE](LICENSE).
