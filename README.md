<p align="center">
  <img src="pkg/sce_sys/pic1.png" alt="PS5Torrent" width="100%">
</p>

<h1 align="center">PS5Torrent</h1>

<p align="center">
  Cliente BitTorrent para PS5 com painel web, acompanhamento em tempo real,
  seleção de armazenamento no console e aplicativo próprio em Mídias.
</p>

<p align="center">
  <img alt="Versão" src="https://img.shields.io/badge/versão-2.0.4-7c5cff?style=for-the-badge">
  <a href="https://github.com/jfcardososantos/PS5Torrent/actions/workflows/build.yml"><img alt="Build" src="https://img.shields.io/github/actions/workflow/status/jfcardososantos/PS5Torrent/build.yml?branch=main&style=for-the-badge&label=build"></a>
  <img alt="Plataforma" src="https://img.shields.io/badge/plataforma-PS5-006FCD?style=for-the-badge&logo=playstation">
  <img alt="Linguagem" src="https://img.shields.io/badge/linguagem-C%20%2B%20C%2B%2B-14b8a6?style=for-the-badge&logo=cplusplus">
  <img alt="Licença" src="https://img.shields.io/badge/licença-GPL--3.0-f59e0b?style=for-the-badge">
</p>

<p align="center">
  <a href="#recursos">Recursos</a> •
  <a href="#como-usar">Como usar</a> •
  <a href="#compilação">Compilação</a> •
  <a href="#estrutura-do-projeto">Estrutura</a>
</p>

> [!IMPORTANT]
> Projeto experimental destinado a pesquisa e homebrew. Use somente torrents
> de conteúdo que você tem autorização para baixar. Requer um PS5 com ambiente
> de homebrew compatível; não utilize uma conta PSN importante durante testes.

## Recursos

- painel responsivo preparado para TV, navegador do PS5 e controle;
- progresso, velocidade, tamanho restante, ETA, seeds e peers em tempo real;
- adição de torrents por arquivo `.torrent` ou magnet link;
- navegador de arquivos próprio para escolher `.torrent` dentro do PS5;
- seleção de armazenamento USB, M.2/NVMe ou interno disponível;
- navegação por pastas no destino de download e criação de nova pasta pelo painel;
- metainfo single-file e multi-file;
- backend original em C para builds leves;
- backend opcional baseado em libtorrent 2.0.12 para maior compatibilidade;
- suporte no backend libtorrent a HTTP/UDP trackers, DHT, LSD, PEX, uTP, magnet metadata e criptografia de protocolo;
- conexões paralelas com peers e controle de sessão pelo libtorrent;
- verificação de integridade das peças antes de concluir o download;
- logs persistentes em `/data/PS5Torrent/PS5Torrent.log` e tela de logs no painel;
- rotação automática do arquivo de log ao atingir 1 MB;
- idioma automático de acordo com o sistema, com suporte a português, inglês e espanhol;
- instalação em Mídias com mensagem de pronto, sem abertura automática;
- fPKG com ícone e arte próprios;
- versão do aplicativo e do pacote definida como 2.0.4.

<p align="center">
  <img src="pkg/sce_sys/pic0.png" alt="Arte do painel PS5Torrent" width="88%">
</p>

## Como usar

### Aplicativo em Mídias

Execute `PS5Torrent.elf` pelo loader. Ao iniciar, ele prepara o serviço local,
instala o pacote incorporado em **Mídias** quando necessário e mostra uma
mensagem quando estiver pronto. Depois disso, abra o PS5Torrent manualmente na
aba Mídias.

O aplicativo de Mídias abre o painel local na porta `12389`. O ELF precisa
continuar ativo enquanto o painel e os downloads estiverem em uso. Após
reiniciar o console, execute o ELF novamente.

### Adicionar torrent

No painel, clique em **Novo torrent**. Você pode:

- escolher um arquivo `.torrent` navegando pelas pastas do PS5;
- enviar um arquivo `.torrent` pelo navegador de outro dispositivo;
- colar um magnet link.

O limite do arquivo de metadados enviado pelo painel é 1 MB. Esse limite vale
somente para o arquivo `.torrent`, não para o tamanho final do conteúdo baixado.

### Escolher destino

Em **Salvar em**, selecione um atalho de armazenamento disponível. Ao clicar em
**Outro caminho**, o painel lista as pastas a partir do atalho selecionado.
Confirme com **Usar esta pasta**.

O botão **Criar pasta** abre o teclado apenas para o nome da nova subpasta. Após
criar a pasta, confirme se ela será o destino do download.

### Consultar logs

O botão **Logs**, ao lado de **Novo torrent**, abre a tela de diagnóstico do
aplicativo. Ela mostra os eventos mais recentes, atualiza automaticamente e lê o
arquivo salvo em:

```text
/data/PS5Torrent/PS5Torrent.log
```

Quando o arquivo chega a 1 MB, ele é movido para
`/data/PS5Torrent/PS5Torrent.previous.log` e um novo arquivo é iniciado.

### Payload ELF

Com um loader ouvindo na porta 9021:

```bash
export PS5_HOST=192.168.1.100
export PS5_PORT=9021
make test
```

Também é possível selecionar `PS5Torrent.elf` em um aplicativo de envio de
payloads. Depois do carregamento, acesse:

```text
http://IP_DO_PS5:12389/
```

Para confirmar pelo Mac o que está respondendo:

```bash
./scripts/check_ps5.sh 192.168.1.100
```

O diagnóstico testa o painel na porta 12389 e os loaders nas portas 9021 e
9020. Quando o serviço está ativo, o estado da API é mostrado no terminal.

## Compilação

### macOS

Pré-requisitos:

- macOS com Command Line Tools (`xcode-select --install`);
- [Homebrew](https://brew.sh).

Prepare o SDK e compile o ELF padrão:

```bash
./setup.sh
```

O script instala/verifica `llvm@18` e `socat`, baixa o ps5-payload-sdk v0.41,
confere seu SHA-256 e gera `PS5Torrent.elf`. Tudo permanece dentro do projeto,
sem `sudo` e sem alterações no `.zshrc`.

Comandos adicionais:

```bash
make                       # recompila o ELF padrão
make clean all             # build limpo
./setup.sh --no-deps       # não executa brew install
./setup.sh --sdk-only      # prepara apenas o SDK
```

### Build com libtorrent

Para gerar o ELF com o backend libtorrent:

```bash
USE_LIBTORRENT=1 ./scripts/build_pkg_macos.sh
```

Esse comando baixa/prepara libtorrent 2.0.12 e Boost 1.84.0, compila o backend
e coloca os artefatos finais em `dist/`:

```text
dist/PS5Torrent.elf
dist/PS5Torrent.pkg
```

Também é possível informar caminhos de cache/build manualmente:

```bash
USE_LIBTORRENT=1 \
LIBTORRENT_DEPS_DIR=/tmp/ps5torrent-libtorrent \
LIBTORRENT_BUILD_DIR=/tmp/ps5torrent-libtorrent-build \
./scripts/build_pkg_macos.sh
```

### Build de laboratório do libtorrent

Há um alvo separado para validar somente a sessão libtorrent no PS5:

```bash
make libtorrent-lab
```

O artefato fica em:

```text
dist/ps5torrent-libtorrent-lab.elf
```

Esse ELF procura por padrão o arquivo:

```text
/data/PS5Torrent/input.torrent
```

ou aceita um magnet/arquivo `.torrent` por argumento, dependendo do loader usado.

### Gerar o fPKG

O pacote de Mídias já acompanha o código. Para conferir e preparar a
distribuição:

```bash
./scripts/build_pkg_macos.sh
```

Para gerar a distribuição com o backend libtorrent, use:

```bash
USE_LIBTORRENT=1 ./scripts/build_pkg_macos.sh
```

Para recriar o pacote após alterar imagens ou metadados, use
`python scripts/build_media_pkg.py` com `prospero-pub-cmd` disponível.

O ELF prepara as próprias permissões, credenciais e raiz do sistema de arquivos
pelas APIs locais do ps5-payload-sdk. O loader e o ambiente homebrew precisam
oferecer suporte às APIs do SDK.

## Limitações conhecidas

- o backend libtorrent ainda precisa de validação ampla em firmwares e loaders diferentes;
- trackers HTTPS dependem do suporte OpenSSL/certificados disponível no ambiente de execução;
- o painel não possui autenticação e deve ficar em uma rede local confiável;
- compatibilidade do fPKG varia conforme firmware, jailbreak e instalador;
- o serviço precisa que o ELF permaneça ativo durante o uso do aplicativo em Mídias.

## Estrutura do projeto

```text
.
├── .github/workflows/     # integração contínua
├── include/               # headers C/C++
├── src/                   # cliente, servidor HTTP e painel incorporado
│   └── web/index.html     # fonte editável da interface
├── src_libtorrent/        # build/laboratório do backend libtorrent para PS5
├── scripts/libtorrent/    # preparação de libtorrent e Boost
├── pkg/                   # metadados, ícone e artes do aplicativo
├── scripts/               # build, diagnóstico e utilitários
├── tools/                 # empacotador fPKG para macOS
├── Makefile               # build do payload
└── setup.sh               # preparação reproduzível do ambiente
```

## Segurança e uso responsável

O PS5Torrent não inclui conteúdo, catálogos ou mecanismos de busca. O usuário é
responsável pelos torrents adicionados e pelo cumprimento das leis e licenças
aplicáveis. Homebrew pode violar os termos de serviço da Sony e expor o console
a riscos.

## Licença

Distribuído sob a licença [GPL-3.0](LICENSE).
