# Validação antes da distribuição pública

| Cenário | Critério |
|---|---|
| Autenticação | Alteração de payload, chave errada e replay são rejeitados |
| Salas | Aprovação obrigatória; quinto viewer não é aprovado |
| Privacidade | Sem TURN antes das duas autorizações; diagnóstico sem segredos |
| WebRTC local | Vídeo VP8 sintético é decodificado por outro peer |
| Pop!_OS COSMIC | Portal seleciona o monitor correto; cancelar/encerrar para captura |
| Linux X11 | Monitor selecionado e cursor aparecem com proporção correta |
| Windows 10 22H2 | Vídeo funciona; áudio seletivo indisponível é indicado |
| Windows 11 | Áudio autorizado é ouvido; som não autorizado não aparece |
| Áudio Linux | Dois apps distintos: somente o marcado é recebido |
| Revogação | Desmarcar/reiniciar/remove stream não reautoriza outro processo |
| Quatro viewers | Todos veem; remover um encerra somente sua conexão |
| Qualidade | Ajustar tamanho/FPS/bitrate altera transmissão sem encerrar sala |
| NAT restritivo | Falha direta oferece relay; recusa mantém mídia fora do servidor |
| UDP bloqueado | TURN/TLS 443 conecta, quando permitido pela rede |
| Dtel ↔ outra operadora | 30 minutos sem queda, monitorando latência/perda/rota |
| Pacotes portáteis | PC sem SDK/Qt/GStreamer abre um único arquivo |

Testes automatizados não substituem captura real, máquinas Windows, NAT de
operadora, bloqueio de UDP ou testes em máquinas limpas. Registrar máquina,
GPU, upload, rota, valores efetivos e resultado por cenário. Meta de 1080p/60
não é uma garantia para qualquer hardware ou upload.

Teste Dtel: começar com vídeo sintético, um viewer e P2P. Exportar diagnóstico
nos dois lados; se falhar, autorizar relay nos dois. Só depois repetir com tela
real e áudio autorizado, e ampliar para quatro viewers. Para afirmar que o modo
direto não envia mídia ao servidor, inspecionar tráfego na VPS durante a sessão;
apenas observar o rótulo da interface não é evidência suficiente.

## Resultados locais — 0.1.0, 1 de outubro de 2026

Ambiente: Pop!_OS 24.04/COSMIC; Qt 6.4.2, GStreamer 1.24.2,
PipeWire 1.6.8 e Python 3.12. Captura real da sessão do usuário não foi usada
nos testes automatizados.

- Autenticação: teste de token, HMAC, alteração de mensagem, segredo incorreto
  e replay passou.
- Mídia: dois peers decodificaram 90 frames VP8; teste com áudio sintetizado
  e teste sem áudio autorizado passaram. Rota direta identificada pelos stats.
- Relay: Coturn local passou com TURN normal, TURN/TCP e TURN/TLS, cada um
  recebendo 90 frames e identificando a rota como relay. Também passou
  com os plugins do pacote final. TLS usou certificado
  temporário e porta alta; não valida firewall externo nem porta pública 443.
- Servidor: oito testes passaram com aiohttp 3.13.3; aprovação, limite de
  quatro, quinto recusado, remoção, expiração, retomada do host, limites de
  entrada/mensagens e ausência de credenciais TURN antes de dupla autorização.
- Sessão completa: quatro janelas viewers receberam vídeo autenticado do host,
  mudaram de 640×360/30 para 320×180/15. Remover um viewer encerrou somente
  sua conexão; os outros três receberam outra alteração para 640×360 e
  encerraram com o host. Teste executado
  com as bibliotecas incluídas no pacote Linux. Uma falha de excesso de mensagens
  durante a negociação simultânea foi corrigida e ganhou teste de regressão.
- Áudio seletivo Linux: servidor PipeWire privado, sem hardware real. Tom
  autorizado de 440 Hz medido em ~0,0489; tom excluído de 880 Hz em ~0,000017.
  Remover autorização esvaziou a fila. As bibliotecas portáteis foram usadas.
- AppImage: verificação de dependências e abertura/fechamento da janela offscreen
  passaram pelo arquivo único com `--appimage-extract-and-run`, sem FUSE.
- A biblioteca libnice emitiu aviso de allocations TURN ainda em limpeza ao
  destruir os peers. As pipelines de mídia encerraram; a limpeza das allocations
  nessa combinação GStreamer/libnice ainda depende também do timeout do TURN.
- Windows: o primeiro pacote foi compilado de forma cruzada e falhou no
  carregador durante o teste do usuário. O substituto foi compilado nativamente
  em Windows/MSYS2 e passou na verificação de símbolos e abertura portátil
  no CI Windows Server 2022, conforme a seção de correção abaixo.

Ainda pendentes: servidor público/domínios (a definir), captura X11 e transmissão real entre PCs
Linux/Windows, captura e áudio reais no Windows 10/11, testes entre máquinas limpas,
IPv6 externo, UDP bloqueado, TLS/443 público e sessão Dtel de 30 minutos.
Não há evidência ainda para aceitar a meta real de 1080p/60 na Dtel.

## Teste LAN/TLS — 0.1.1

Servidor local com certificado próprio e impressão SHA-256 explícita no cliente.
Teste `server/tests/local_tls.py`: quatro viewers, qualidade, remoção e
encerramento passaram com a impressão correta; a impressão incorreta foi
recusada. Servidor escutando somente a interface privada configurada, sem TURN.

Relato do primeiro teste real: AppImage fecha depois da seleção do monitor.
Reproduzido com a janela real sob gdb: `QDBusArgument: write from a read-only
object`, seguido de SIGABRT em `QDBusArgument::operator>>(unsigned int&)`.
A leitura usava `beginArray`/`beginStructure` sobre variáveis mutáveis, acionando
as sobrecargas de escrita do Qt em um argumento recebido do portal.

Regressão `dbus-run-session -- build/portal-test`: uma resposta ScreenCast
serializada por outro processo reproduziu o mesmo aborto (saída 134), sem
precisar selecionar monitor. Após usar argumentos const e validar as assinaturas
`a(ua{sv})` e `(ii)`, passou. Inclui respostas vazias e tamanhos inválidos.
Captura real foi revalidada no mesmo COSMIC com seleção manual: a sala foi
criada, frames reais em 1920×1080 foram capturados continuamente, sem o aborto
anterior. FPS efetivos observados variaram (aproximadamente 21–46), portanto
este teste não aceita ainda a meta sustentada de 60 FPS. Inicialmente não havia
viewer: upload zero era esperado.

`scripts/diagnose-capture.sh` prepara um teste manual do pacote, sem viewers,
para registrar a pilha de funções em caso de falha. Argumentos de funções não
são impressos; não há gravação de tela. O diagnóstico é exportado somente
quando esse roteiro é executado manualmente.

## Integridade e aceleração — 0.1.2, 2 de outubro de 2026

Relato: imagem fragmentada e captura ~15 FPS com um viewer local. A fila de
recepção descartava pacotes RTP antes do depayloader; dois buffers são dois
fragmentos, não dois quadros. Em 1080p/60, o teste sintético de integridade
falhou com zero quadros em oito segundos. Após manter os fragmentos e descartar
somente quadros completos no appsink, recebeu 463 quadros (VP8/CPU), sem quadros
corrompidos. A cópia rasa de buffers mantém agora referência explícita ao buffer
pai, para que a memória de pools de captura não seja reciclada durante uso.

GPU local: Intel Alder Lake-UP3 GT2/Iris Xe, driver iHD 24.1.0. Tem encode
H.264, mas não encode VP8. O app seleciona H.264 após um teste real de oito
quadros, incluindo decodificação. VA-API local recebeu 442 quadros em oito
segundos a 1920×1080/60; nenhum erro médio de pixels acima do limite do teste
(10/255 por canal, barras estáticas do SMPTE). Estes números incluem negociação
e início da conexão; não validam 60 FPS sustentados de captura real.

Plugins VA, NVENC e Quick Sync são distribuídos mesmo quando a máquina de
compilação não registra factories de GPU. Sem encoder funcional, há fallback
VP8 por CPU. H.264 usa OpenH264 para decodificação. NVIDIA e Windows/Quick Sync
foram preparados, mas não executados em hardware correspondente. Captura usa I420 compartilhado; a conversão para NV12 força a renegociação da
alocação VA ao mudar de resolução. O caminho NV12 sem conversão retinha um pool
com tamanho anterior; o teste de quatro viewers reproduziu a falha e passou
após o ajuste, incluindo 640×360 → 320×180 → 640×360, remoção e encerramento.
As métricas de FPS por viewer precisam de revalidação manual na sessão COSMIC. Continuam pendentes os testes reais entre PCs e Dtel.

Verificação do pacote final: `--check-encoder` no AppImage selecionou VA-API no
Intel real. Com somente os plugins/bibliotecas do AppDir, recebeu 467 quadros
em oito segundos (1080p/60 alvo), sem quadros corrompidos detectados. TURN UDP,
TCP e TLS também receberam 90 quadros H.264 por teste. Os seis testes CTest e
o teste LAN/TLS com quatro viewers passaram. AppImage abriu sem FUSE; o
Windows original passou somente pela verificação dos nomes de dependências PE,
que não detectou a incompatibilidade C++ encontrada depois. Captura real
em Windows permanece pendente; o teste de abertura corrigido está descrito abaixo.

## Correção do pacote Windows — 2 de outubro de 2026

O primeiro teste em Windows encontrou uma falha do carregador antes da janela:
`_ZSt21ios_base_library_initv` ausente em `libstdc++-6.dll`. O build cruzado
com GCC 13 exigia esse símbolo, mas a DLL distribuída pelo outro ambiente
não o exportava. A verificação dos nomes de DLLs não detectava essa incompatibilidade.

O empacotamento agora compara os símbolos importados de todos os executáveis
e plugins com as exportações das DLLs incluídas. Essa verificação reproduziu
a falha no pacote original. O executável distribuído deve vir do build nativo
Windows/MSYS2, com compilador, Qt e GStreamer do mesmo ambiente.

O CI executa o próprio pacote portátil com `--check-runtime` e `--smoke-test`,
com o PATH sem as ferramentas de desenvolvimento. Esses testes incluem extração,
carregamento das bibliotecas/plugins e abertura da janela. Captura, áudio e
conexão entre PCs continuam dependendo dos testes em equipamentos reais.

Resultado: [build bc3cead no GitHub Actions](https://github.com/LazaroLanderson/lazarus-share/actions/runs/37039599647)
concluiu o job Windows com sucesso: teste de protocolo, verificação de imports/exports,
`--check-runtime` e `--smoke-test` pelo EXE portátil. O runner usa Windows Server
2022; isso valida o carregador, extração e abertura, mas não comprova captura/áudio
em Windows 10/11 nem aceleração em GPUs físicas.

## HTTPS temporário pelo IP — 0.1.3

O deploy por IP usa certificado autoassinado com SAN IPv4 e o healthcheck confia
explicitamente nesse certificado, sem desativar verificação TLS. O cliente
Windows/Schannel pode apresentar `CertificateUntrusted` para essa raiz; o pin
passa a aceitar esse erro somente para um certificado autoassinado cujo SHA-256
corresponde exatamente ao valor configurado. Mismatch de hostname/IP, expiração,
revogação e pin incorreto continuam recusados.

O teste de política reproduziu a rejeição anterior e passou depois do ajuste.
O teste de conexão TLS real verifica um servidor por IP com o backend nativo Qt,
pin correto, pin substituído e SAN incompatível; o job Windows executa esse teste
antes de empacotar.


## 0.2.0 — validação nesta implementação

Passaram localmente: perfil Unicode e persistência, remoção de segredos de logs,
protocolo, mídia/qualidade por CPU e Intel, portal, quatro viewers aprovados sem
captura inicial, compartilhar/parar/retomar, fallback automático UDP/TCP/TLS com
Coturn real e recusa de relay. Servidor cobre cinco viewers, perfis inválidos,
renovação/revogação TURN, reconexão e limites. Build nativo Windows e AppImage
são validados pelo workflow de build; conferir a execução associada à release.

DNS/certificado público, TURN externo na VPS, testes reais Linux/Windows,
Windows 10/11 e sessão de 30 minutos na Dtel devem ser registrados quando realizados.
O caso específico do viewer Windows ainda não foi reproduzido nesta implementação.

### Validação pública em 2026-10-03

DNS direto da VPS, certificado público ACME, renovação programada, deploy e HTTPS
passaram. A primeira validação externa confirmou mídia TURN por UDP, mas TCP
recebeu erro 486: a reserva de 20 MB/s por alocação esgotava o teto global de
40 MB/s após duas alocações. O teste isolado reproduziu a recusa na terceira
alocação; a redução para 5 MB/s manteve o teto global e passou com oito alocações
simultâneas. A correção foi aplicada na VPS e a revalidação externa passou com mídia
criptografada por UDP, TCP e TLS, além de HTTPS/WSS e aprovação dos participantes.

- [Deploy e teste de oito alocações](https://github.com/LazaroLanderson/lazarus-share/actions/runs/37126645970).
- [Builds nativos Linux e Windows](https://github.com/LazaroLanderson/lazarus-share/actions/runs/37126645989).
- [Validação pública HTTPS/WSS e mídia TURN UDP/TCP/TLS](https://github.com/LazaroLanderson/lazarus-share/actions/runs/37126781531).

A VPS executa o commit `461e8022f507f8549cec082ba2b551e7608f34e5`; o timer
de renovação do certificado está ativo. Testes em PCs reais, Windows 10/11 e
30 minutos na Dtel continuam pendentes, incluindo o caso específico relatado.
