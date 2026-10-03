# Desempenho e recuperação de mídia — 0.2.1

Relatório em preparação; a matriz completa está em execução. Nenhum ganho de CPU é declarado antes do resultado.

## Método reproduzível

`media-benchmark` executa um host e 1/2/4 viewers em processos separados. Usa vídeo sintético I420 com identificador visual de frame e checksum, sockets locais privados para sinalização, WebRTC local sem STUN/TURN e mídia DTLS-SRTP. Não captura tela, não acessa a VPS e não exporta SDP ou conteúdo de imagem. O registro de apresentação associa o identificador decodificado ao relógio monotônico compartilhado do mesmo sistema; RTT não é usado como latência de apresentação.

Cada combinação Baixa/Alta/Nativo × 1/2/4 viewers × software/hardware usa 15 segundos de aquecimento e 60 segundos de medição, três vezes por versão. A ordem 0.2.0/0.2.1 alterna entre repetições. Os cenários são sequenciais, evitando disputa entre benchmarks. A referência 0.2.0 é o commit `461e8022f507f8549cec082ba2b551e7608f34e5`, compilado com o mesmo fixture e dependências. O fixture possui caminho de renderização legado para essa referência.

Equipamento local: Intel Core i5-1235U, Intel Iris Xe, 12 threads; monitor reportado 1920×1080. Portanto Alta e Nativo têm a mesma resolução nesta máquina, mas são executados separadamente. Os resultados não demonstram comportamento em 4K/HiDPI.

A execução usa renderização raster/offscreen nas duas versões: medimos até a pintura síncrona do componente, sem comprovar apresentação física no monitor. O caminho OpenGL é verificado separadamente por integridade de pixels no CI com Mesa/Xvfb e no contexto real do Intel Iris Xe local. Esses testes não medem o desempenho ou a apresentação física do OpenGL.

CPU por processo usa `/proc`: 100% corresponde a um núcleo, podendo exceder 100%. Memória é RSS. A atividade de GPU usa contadores DRM de tempo por engine/contexto quando disponíveis; não equivale a uma porcentagem global do chip e pode somar mais de 100%. Falta de contador será indicada como indisponível. Custos do host e dos viewers locais são separados.

O fixture copia e carimba um frame sintético por captura nas duas versões; esse custo não existe dessa forma na captura real. Descarte conhecido é medido por sobrecarga da fila de vídeo e substituição de frames decodificados; `frames_pending_estimate` é uma diferença de contadores, não um descarte confirmado. Quando o pool NV12 de oito buffers está ocupado, o frame preparado é descartado antes de codificar, sem alternar as caps para I420; esse descarte tem contador próprio. A fila appsrc também é limitada e pode descartar sem contador disponível nas versões antigas do GStreamer. Na aplicação real, a captura registra frames substituídos antes da distribuição e tempo médio da conversão compartilhada.

A 0.2.1 correlaciona estatísticas pelo SSRC de vídeo, inclusive quando GStreamer marca incorretamente o tipo em BUNDLE. Contadores de áudio são excluídos. A 0.2.0 usava os contadores legados; o fixture transmite apenas silêncio de manutenção. Feedback ausente não é inventado e mantém o bitrate.

## Executar

Compile `media-benchmark` na versão atual. O helper abaixo extrai apenas Peer/Encoder do commit publicado e compila a referência com o mesmo fixture e dependências. Precisa desse commit no histórico local; não altera o checkout atual. Use um diretório vazio fora do repositório:

```sh
python3 scripts/build-benchmark-baseline.py --output /tmp/lazarus-baseline
python3 scripts/benchmark-media.py --baseline /tmp/lazarus-baseline/build/media-benchmark
```

O arquivo `dist/benchmarks/0.2.1.jsonl` guarda resultados agregados e hashes dos binários, sem PIDs, caminhos pessoais, IPs, SDP, credenciais ou frames. Pode retomar uma execução interrompida quando hashes e parâmetros continuam iguais. Gere a comparação com `python3 scripts/report-media-benchmark.py dist/benchmarks/0.2.1.jsonl`; o comando recusa matrizes incompletas, hashes misturados e gates com regressão. Os logs do app continuam sujeitos à política de 7 dias/10 MB; este arquivo é um artefato de teste exportado deliberadamente.

## Aceite

Buscar redução de 10% de CPU do host com quatro viewers; publicar o ganho efetivamente observado. Para cenários comparáveis, não aceitar queda de FPS maior que 5% ou aumento da latência p95 maior que 10%. Três repetições não eliminam ruído de carga/temperatura: regressões devem ser investigadas e repetidas antes do aceite.

## Verificações e limitações

Testes locais cobrem módulos de bitrate, conversão/pixels, watchdog de cinco segundos, software, qualidade, vídeo sem áudio, quatro viewers, recuperação isolada, callback antigo, falha terminal, pausa/retomada e fallback TURN UDP/TCP/TLS. CI nativo Linux/Windows e inicialização dos pacotes serão registrados com seus resultados.

Ainda pendentes: NVIDIA real, Quick Sync no Windows, vários dispositivos de GPU, monitores 4K/HiDPI, captura física Wayland/Windows e o caso do Windows com 0 kbps. A sessão sintética local não substitui testes entre máquinas ou de longa duração na Dtel.

O bitrate informado é de vídeo RTP. Áudio autorizado, cabeçalhos adicionais e retransmissões podem aumentar o upload real da interface de rede. `frames_pending_estimate` não é usado para declarar descartes; a referência 0.2.0 não tinha o contador de descarte conhecido por fila e exibição da nova versão.
