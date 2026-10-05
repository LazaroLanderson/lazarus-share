# Protocolo v2 e limites de privacidade

## Compatibilidade e convite

Criação, retomada e entrada exigem `protocol: 2`. Versões anteriores recebem
`error: protocol_update_required`; não existe modo de sala v1 nesta implementação.
`created`, `joined`, `ready` e `room-state` identificam o protocolo. Clientes novos
recusam `created`/`joined` de servidores antigos com orientação para atualizar o serviço.
Atualizar o servidor antes de distribuir os clientes v2. Os clientes 0.2.6 usam v2; executáveis 0.2.5 e anteriores precisam atualizar.

O convite continua sendo 16 bytes aleatórios em Base32, sem padding, em
`https://share.app.lazaruslabs.com.br/join#TOKEN`. A página encaminha o fragmento
para `lazarus-share://join#TOKEN`, sem enviá-lo ao servidor. Não são aceitos
credenciais, query ou porta explícita. A identificação da sala continua
SHA-256(`lazarus-share/room/v1:` + segredo), preservando o formato dos convites.
O serviço nunca recebe o token/segredo. A credencial de administração é
independente, de 32 bytes; apenas seu hash permanece na memória do servidor.
WSS é obrigatório fora de loopback; TLS local pode usar a impressão exata do certificado.

## Administração e participantes

`create`, `resume` e `join` recebem desafio de 128 bits, perfil e capacidades.
`created`/`joined` devolvem `peer`, o ID do próprio participante. O criador tem
um ID estável durante sua retomada; convidados recebem novo ID e precisam de
nova aprovação, quando exigida, após reconectar.

`requireApproval` é booleano, imutável na sala; omitido, usa `true` por segurança
para integradores. O aplicativo envia `false` por padrão. O criador usa
`approve {peer}`, `remove {peer}` e `end`. Convidados não administram a sala.
São permitidos cinco participantes aprovados no total, incluindo o criador,
e até dezesseis convidados contando a fila de aprovação.

`room-state` contém `protocol`, `participants`, `broadcaster`, `revision`,
`sharing` e `ownerOnline`. Cada participante listado contém `peer`, `profile`,
`approved` e `owner`. Somente o administrador vê candidatos pendentes; um
candidato recebe `joined` e aguarda aprovação antes de receber o estado da sala.

Nickname é normalizado NFC, com até dez grafemas/80 unidades Unicode e sem
controles. Avatar é inteiro de 0 a 9. Nomes não são identidades verificadas.
`profile {profile}` atualiza o roster; o perfil também circula no canal autenticado
quando existe uma conexão. A lista permanece somente em memória.

Saída explícita do criador encerra todos os participantes. Sua queda emite
`host_offline` e mantém a sala por até 60 segundos para `resume`, autenticado
pela credencial de administração. Não há transferência de administração.

## Exclusividade do compartilhamento

Participantes aprovados enviam `share-request`. O servidor, sob trava por sala,
reserva uma única vez por até 60 segundos. O primeiro pedido vence; os demais
recebem `share_busy`. A reserva define `broadcaster`, incrementa `revision` e
notifica todos com `sharing: false` antes de abrir o seletor de captura.

O transmissor confirma com `share-confirm {revision}` quando a captura estiver
pronta. O servidor publica `sharing: true` e cria um par para cada receptor
aprovado e conectado. A confirmação é idempotente. `share-release {revision}`,
expiração da reserva, remoção ou queda do transmissor liberam a vez, incrementam
a revisão e descartam todos os pares. Pedidos de outra pessoa ou de uma revisão
anterior são recusados. Reconectar não retoma captura automaticamente.

Uma entrada/aprovação durante transmissão cria somente o novo par. Retomar o
administrador enquanto um convidado transmite recria o par dele com nova sessão
e desafio, preservando os outros receptores. Mudanças de monitor/qualidade mantêm
a vez e renegociam a mídia; cancelamento da captura inicial libera a reserva.

## Canal autenticado e mídia

`ready` identifica o peer remoto, `session`, seu desafio, perfil, capacidades e
`revision`. O transmissor conecta diretamente a cada receptor, inclusive ao
criador quando este assiste. O computador do criador não encaminha mídia.

`signal {peer, revision, payload, mac}` usa o ID de destino. O serviço só
encaminha pares ativos transmissor–receptor da revisão atual; a mensagem recebida
identifica o ID de origem em `peer`. O payload é JSON compacto em Base64 com
HMAC-SHA256. A chave é derivada do segredo do convite com domínio
`lazarus-share/signaling/v2`.

O envelope assinado contém `session`, `sender`, `recipient`, `revision`, desafio
do destinatário, sequência crescente e `body`. O destinatário valida todos esses
campos, HMAC e proteção de replay antes de aplicar SDP, ICE ou controles. Callbacks
locais também validam sessão, revisão e geração, impedindo que uma conexão antiga
altere a transmissão nova. Todos que conhecem o convite compartilham a chave;
não há autenticação independente contra um participante malicioso com o token.

O transmissor coordena `restart {generation, relay, transport}`: -1 para P2P e
0/1/2 para TURN UDP/TCP/TLS. `retry-request` solicita nova tentativa;
`connection-failure` informa falha do receptor e `connection-terminal` informa
esgotamento das tentativas ao receptor. Perfil e `relay-consent {enabled}` também
são autenticados. O antigo body `sharing` foi substituído pelo estado revisionado.

Cada par usa `webrtcbin`, H.264 por hardware ou VP8 por CPU, Opus e DTLS-SRTP.
Captura ocorre uma vez; codificação e bitrate são adaptados por receptor.
Queues limitadas impedem crescimento indefinido de memória/latência.

## Relay e recuperação

A tentativa inicial usa somente P2P/STUN. TURN exige aprovação de entrada e
consentimento dos dois integrantes do par, tanto no canal autenticado quanto
em `relay {peer, revision, enabled}` para o servidor. O consentimento do criador
não substitui o de um receptor ou transmissor convidado.

O transmissor usa `relay-request` após falha direta ou para renovar credenciais.
`turn` identifica peer, revisão, endpoints, host, username, password e expires.
Credenciais duram uma hora; emissão por par é limitada a uma vez a cada 30 segundos.
Trocar transmissor descarta pares, consentimentos e credenciais daquela transmissão.

Renovação começa cinco minutos antes da expiração, sem reiniciar mídia ativa.
O prazo é 15 segundos; repetição após falha em 30, 60, 120 e depois 300 segundos.
Respostas antigas, duplicadas, expiradas ou de outra revisão são ignoradas.
Revogar consentimento encerra o relay e cancela operações pendentes.

Falha do encoder permite uma tentativa por software, mantendo o transporte e
incrementando a geração somente do receptor afetado. Falha do decoder H.264
permite uma tentativa por software; ausência de resposta por 15 segundos termina
a recuperação. Falhas posteriores exigem nova tentativa manual. Falha do áudio
encerra somente a reprodução de áudio e preserva vídeo/transporte.

## Viewer, áudio e dados locais

O viewer oferece ajuste proporcional, 100% com rolagem e tela cheia (F11/Esc).
Volume de 0–100% e mute são aplicados depois da decodificação no pipeline local,
nunca alterando captura, envio ou áudio dos demais receptores. A preferência
permanece durante reconexões e trocas de transmissor no aplicativo aberto.

O estado “Ao vivo” só aparece após vídeo recebido. Após cinco segundos sem um
novo quadro, o viewer mantém a última imagem com aviso visível e contador, sem
reiniciar transporte ou parar áudio funcional. Quadros idênticos recebidos
continuamente não geram aviso. Parada/troca de transmissão limpa a imagem e o aviso.

Captura de áudio continua restrita aos aplicativos explicitamente selecionados,
sem microfone nem saída global. Linux identifica streams por serial PipeWire;
Windows por PID e instante de criação. Reinício exige nova seleção.

Servidor: salas, hashes, perfis, desafios, pares, revisões e contadores em RAM.
Rate limit mantém IPs por no máximo um minuto. SDP autenticado não é cifrado além
do TLS: sinalização pode ver IPs candidatos; TURN vê endpoints e tráfego, sem
ler a mídia. O diagnóstico permanece local e é exportado manualmente, sem convite,
segredos, IPs, aplicativos, SDP ou credenciais. Não há gravação nem telemetria.
