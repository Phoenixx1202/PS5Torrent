# Changelog

Todas as mudanças relevantes do PS5Torrent serão registradas neste arquivo.

## [2.0.4] - 2026-09-06

- mensagem de falha dos trackers não atribui problemas HTTP/UDP ao HTTPS; log identifica DNS, TCP, envio, timeout e status HTTP;

- correção do download após conexão: estados choke/unchoke, bitfield/HAVE e blocos de até 16 KiB;
- retomada da mesma peça após choke/unchoke, sem perder blocos já recebidos;
- ação de iniciar idempotente quando o torrent já está ativo ou completo;
- montagem da peça completa antes de validar SHA-1 e confirmar gravação, com nova tentativa após desconexão/hash inválido;
- logs de transferência, gravação e conclusão; testes locais de download completo e falhas;

- suporte a trackers UDP IPv4 (BEP 15), com validação de transações e repetição após perda de pacote;
- suporte a web seeds HTTP (`url-list`/BEP 19) como fallback quando trackers/peers não produzem conexão útil;
- diagnóstico por peer para falhas de TCP, handshake e envio de interested;
- logs de protocolo, endpoint e quantidade de peers por tracker, sem expor caminhos/chaves privadas;

- logs persistentes na pasta `/data/PS5Torrent`, com rotação de arquivos;
- tela preta com logs brancos, atualização automática e botão ao lado de Novo torrent;

- descoberta de trackers/peers em segundo plano, sem bloquear o painel ao adicionar torrent;
- mensagens incompletas de peers não bloqueiam o loop, e mensagens grandes demais são rejeitadas;
- redução de uso da pilha no upload/leitura de peers e validação de metadados/alocações;

- seleção de destino por pastas a partir do atalho escolhido, sem teclado para caminhos;
- criação de subpastas com teclado apenas para o nome e confirmação do destino;

- proteção contra SIGPIPE ao desconectar o navegador ou um peer;
- interrupções por sinais não encerram o loop HTTP;
- espera pelo registro do PKG sem bloquear o atendimento HTTP por 30 segundos;

- correção do falso erro de instalação ao encontrar `app.pkg` e metadados em `appmeta`;
- configuração de permissões diretamente pelo SDK, sem servidor de comandos do etaHEN;
- ícones SVG de pasta e arquivo .torrent, sem depender de emojis do navegador;

- seletor de pastas e arquivos .torrent do PS5, evitando o seletor nativo associado ao CE-116960-3;
- instalação do PKG de Mídias incorporado no ELF, seguida da mensagem de aplicativo pronto, sem abertura automática;
- ícone e fundo fornecidos pelo usuário nos assets do PKG;
- identificação como `PS5Torrent.elf` no Payload Manager;
- idioma automático do sistema: português, inglês e espanhol;
- testes locais de navegação, envio, tradução, limites de leitura e paginação;

## [1.0.0] - 2026-07-21

Primeira versão pública.

### Incluído

- cliente BitTorrent em C para PS5;
- painel web responsivo com progresso, velocidade, ETA e peers;
- upload de arquivos `.torrent` e seleção de armazenamento;
- abertura automática do navegador e notificações do sistema;
- diagnóstico de rede pelo macOS;
- payload ELF compilado com ps5-payload-sdk v0.41;
- fPKG experimental com ícone próprio na tela principal;
- geração do ELF e do fPKG inteiramente pelo macOS;
- build automatizado no GitHub Actions.
