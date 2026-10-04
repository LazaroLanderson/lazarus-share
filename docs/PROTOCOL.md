# Protocolo v1 e limites de privacidade

## Convite e autenticação

Convite = 16 bytes aleatórios codificados em Base32, sem padding (26 caracteres),
compartilhados como `https://share.app.lazaruslabs.com.br/join#TOKEN`. A página
encaminha o fragmento para `lazarus-share://join#TOKEN`, sem enviá-lo ao servidor.
O cliente aceita somente esses formatos, sem credenciais, query ou porta explícita.
O servidor recebe somente SHA-256(`lazarus-share/room/v1:` + segredo), nunca o
token ou seu segredo. A credencial de administração é independente, 32 bytes
aleatórios; o servidor conserva seu hash para retomada do host.

O WebSocket aceita `create`, `resume`, `join`, `approve`, `remove`, `signal`,
`relay` e `end`. O servidor devolve `created`, `joined`, `waiting`, `ready`,
`signal`, `turn`, `left`, `host_offline`, `ended` ou `error`.

`create` aceita `requireApproval` booleano: o app atual envia `false` por padrão;
se omitido, o servidor usa `true` para compatibilidade. A política é imutável durante
a sala e preservada em `resume`. `created` e `joined` incluem `requireApproval`.
Salas abertas emitem `joined` e `ready` automaticamente, sem `waiting`; o quinto
espectador recebe `error: room_full` sem ser inserido. Salas com aprovação mantêm
a fila (até 16 candidatos) e o limite de quatro admissões por `approve`.

Cada `ready` inclui peer, session e desafio do outro lado. Cada cliente cria um
desafio próprio de 128 bits; a sinalização não pode substituir esse desafio
local. Um frame `signal` carrega JSON compacto codificado em Base64 e HMAC-SHA256,
com chave derivada por HMAC do segredo do convite e domínio do protocolo. O
envelope assinado inclui session, peer, sender, desafio do destinatário,
sequência crescente e body. SDP, fingerprints, ICE, consentimento e reinícios
são autenticados. Sessões e desafios impedem replay após reconexão.

Todos que possuem o convite conhecem a mesma chave. O protocolo protege contra
um serviço de sinalização que desconheça esse segredo; não oferece identidades
independentes nem proteção contra um participante malicioso que já tem o token.
Não compartilhe o convite publicamente. WSS continua obrigatório fora de loopback.

## Mídia e relay

Uma conexão `webrtcbin` por viewer: H.264 (hardware disponível) ou VP8 (CPU), e Opus, DTLS-SRTP. O host captura uma
vez, mas codifica/envia por viewer para adaptar bitrate individualmente. As
filas são limitadas para evitar crescimento de memória e latência. Resolução
é uma caixa máxima preservando proporção; FPS depende da captura e do PC.

A conexão inicial não contém servidores TURN. Depois de falha, cada lado envia
consentimento assinado ao peer e pedido ao servidor. O cliente exige seu próprio
consentimento e o consentimento autenticado do outro lado antes de aceitar TURN.
O servidor também exige as duas autorizações e aprovação da entrada.

O host inicia nova geração de conexão após autorização. O viewer espera seus
próprios dados TURN antes de aplicar esse reinício. Credenciais vão pela conexão
TLS, expiram após uma hora e são autenticadas pelo segredo exclusivo do Coturn.
Relay nunca é ativado por uma tentativa automática de reconexão sem autorização.

## Áudio

Linux enumera apenas `Stream/Output/Audio` por serial PipeWire, excluindo o app.
Captura apenas os seriais marcados; não captura saída global ou microfone.
Não reconecta automaticamente um stream desaparecido a outro dispositivo.
Windows enumera processos com sessões de áudio e usa PID + instante de criação
como identidade; captura árvores de processos autorizadas pela API WASAPI.
Reinícios precisam de nova seleção. Remoção de seleção fecha a captura e descarta
fila local; áudio já em trânsito pode ser ouvido pela duração do buffer de reprodução.

## Dados temporários

Servidor: salas, hashes, desafios, conexões, aprovação e contadores em memória.
SDP autenticado não é cifrado no canal de sinalização além do TLS: o servidor pode
ver IPs dos candidatos. TURN vê endpoints e volume de tráfego, sem decifrar a mídia.
Salas expiram 60 segundos após queda do host. Rate limit guarda IPs por até 60s.

Cliente: parâmetros na memória; cache técnico de plugins GStreamer local.
Diagnóstico exportado manualmente inclui métricas e capacidades, sem convite,
segredos, IPs, nomes de aplicativos, SDP ou credenciais TURN. Não há envio automático.


## Extensões aditivas da 0.2.0

`create`/`resume`/`join` podem conter `profile: {nickname, avatar}` e a lista
`capabilities` (`profile`, `sharing`, `turn-endpoints`). `waiting` e `ready`
repassam o perfil e capacidades do outro participante. Perfil permanece somente
na RAM do serviço. Nickname é normalizado NFC, no máximo 10 grafemas/80 unidades
Unicode no cliente, sem controles; avatar é inteiro de 0 a 9. Nomes não são identidades
verificadas. Após aprovação, `profile` também circula no body autenticado.

Sala não implica captura. Body `sharing {enabled}` informa transmissão/pausa.
`restart {generation, relay, transport}` inicia nova tentativa; transport -1 indica
P2P e 0/1/2 correspondem a UDP/TCP/TLS. Somente o host coordena reinícios.
`relay-consent {enabled}` transmite a preferência local autenticada. Consentimento
persistido é reaplicado à nova sessão, mas o servidor só recebe os pedidos `relay`
após o body `relay-request` coordenado pelo host em falha direta ou renovação. Sem dupla autorização,
não se configura TURN. `retry-request` solicita nova tentativa ao host.

Resposta `turn` acrescenta `endpoints` (URLs sem credenciais), além de host,
username, password e expires. Credenciais expiram em uma hora; reemissão exige
participante aprovado, duas permissões e intervalo mínimo de 30 segundos.
Clientes renovam cinco minutos antes da expiração e reaproveitam credenciais ainda
válidas ao retomar uma transmissão na mesma sala. Desabilitar a preferência encerra
imediatamente a rota relay e revoga o consentimento no servidor.

Clientes 0.1.4 continuam com o comportamento antigo, sem nickname e pausa visual.
Atualizar todos os participantes é recomendado. Criptografia e envelope v1 não mudam.

### Recuperação de mídia — 0.2.1

O controle autenticado `restart` aceita `reason` opcional (`encoder_fallback`).
O host incrementa a geração somente para o viewer afetado, mantém `transport`
e o consentimento de relay e renegocia SDP/codec. Mensagens e callbacks de gerações
anteriores são descartados. O viewer 0.2.0 ignora o motivo opcional e recebe a
negociação normalmente; atualizar ambos continua recomendado.

Erros locais são classificados como `encoder_start`, `encoder_error`,
`encoder_stall`, `software_unavailable`, `decoder_error` ou `media_pipeline`;
falhas ICE/DTLS seguem no fluxo de transporte. Um erro de encoder permite uma
única recuperação por software por conexão/configuração; falha posterior aguarda
nova tentativa manual. `encoder_stall` indica ausência de saída, não prova defeito
no hardware. Criar/reconectar uma sala parada não inicia captura.

### Renovação TURN — 0.2.2

Renovação é uma solicitação independente, sem mudar geração, codec, encoder,
rota ou `Peer`. O host usa as mensagens existentes `relay-request`/`relay`, e a
sinalização continua entregando `turn` a ambos. As novas credenciais ficam em
cache para futuras alocações; a alocação ativa mantém seu usuário original e
libnice renova sua duração. Não se adicionam servidores ao ICE em execução.

Prazo de resposta: 15 segundos. Repetições após falha: 30, 60, 120 e depois
300 segundos, com somente uma solicitação pendente por viewer. Erros de emissão
não esgotam a conexão de mídia ativa. O timestamp TURN REST do username impede
respostas antigas/duplicadas de prolongar validade; a validade é limitada por
`expires` e pela expiração absoluta, usando relógio local correto. A sinalização
não mudou; sua limitação de 30 segundos entre emissões continua aplicada.

Uma recuperação real só inicia nova alocação com credenciais válidas. Parar ou
revogar consentimento cancela solicitações e negociações pendentes; respostas
posteriores não iniciam mídia. Atualizar o host é necessário para evitar o
reinício antigo; atualizar os dois participantes cobre ambos os sentidos.

Eventos locais permitidos: `turn_renewal_requested`, `turn_renewal_completed`,
`turn_renewal_failed` e `turn_renewal_retry`; sem credenciais, IPs ou SDP.
