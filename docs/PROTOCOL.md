# Protocolo v1 e limites de privacidade

## Convite e autenticação

Convite = 16 bytes aleatórios codificados em Base32, sem padding (26 caracteres).
O servidor recebe somente SHA-256(`lazarus-share/room/v1:` + segredo), nunca o
token ou seu segredo. A credencial de administração é independente, 32 bytes
aleatórios; o servidor conserva seu hash para retomada do host.

O WebSocket aceita `create`, `resume`, `join`, `approve`, `remove`, `signal`,
`relay` e `end`. O servidor devolve `created`, `joined`, `waiting`, `ready`,
`signal`, `turn`, `left`, `host_offline`, `ended` ou `error`.

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
