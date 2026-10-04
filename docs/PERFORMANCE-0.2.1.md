# Desempenho e recuperação de mídia — 0.2.1

Medições concluídas no Intel Iris Xe em 3–4 de outubro de 2026 (UTC). A matriz inicial teve 108 execuções, três por versão em cada combinação. Após uma repetição intercalada de seis execuções, as 18 combinações passaram nos limites de FPS e latência. Os dados iniciais e a repetição estão preservados; não houve mudança no código entre elas.

A meta de reduzir 10% de CPU do host com quatro viewers foi atingida em Baixa/hardware: **21,9% menos CPU**, mantendo aproximadamente 30 FPS, com p95 de 72 → 32 ms. Em Baixa/software, a redução foi 8,6%. Em Alta/Nativo com quatro viewers, o host produziu mais frames e o atraso caiu, mas a CPU total aumentou entre 4,9% e 14,1%: **o ganho de CPU não é uniforme**. O custo de CPU por frame caiu nessas combinações, mas isso não substitui a meta de custo total.

A captura/conversão ainda passa pela CPU e há um encoder independente por viewer. A disputa dos viewers locais pela mesma CPU/GPU afeta este teste; separar seus custos não elimina essa disputa. Em Alta/Nativo com quatro viewers, este equipamento não sustentou 60 FPS. O app mantém o alvo escolhido e informa quando software fica abaixo dele.

## Método reproduzível

`media-benchmark` executa um host e 1/2/4 viewers em processos separados. Usa vídeo sintético I420 com identificador visual de frame e checksum, sockets locais privados para sinalização, WebRTC local sem STUN/TURN e mídia DTLS-SRTP. Não captura tela, não acessa a VPS e não exporta SDP ou conteúdo de imagem. O registro de apresentação associa o identificador decodificado ao relógio monotônico compartilhado do mesmo sistema; RTT não é usado como latência de apresentação.

Cada combinação Baixa/Alta/Nativo × 1/2/4 viewers × software/hardware usa 15 segundos de aquecimento e 60 segundos de medição, três vezes por versão. A ordem 0.2.0/0.2.1 alterna entre repetições. Os cenários são sequenciais, evitando disputa entre benchmarks. A referência 0.2.0 é o commit `461e8022f507f8549cec082ba2b551e7608f34e5`, compilado com o mesmo fixture e dependências. O fixture possui caminho de renderização legado para essa referência.

Equipamento local: Intel Core i5-1235U, Intel Iris Xe, 12 threads; monitor reportado 1920×1080. Portanto Alta e Nativo têm a mesma resolução nesta máquina, mas são executados separadamente. Os resultados não demonstram comportamento em 4K/HiDPI.

A execução usa renderização raster/offscreen nas duas versões: medimos até a pintura síncrona do componente, sem comprovar apresentação física no monitor. O caminho OpenGL é verificado separadamente por integridade de pixels no CI com Mesa/Xvfb e no contexto real do Intel Iris Xe local. Esses testes não medem o desempenho ou a apresentação física do OpenGL. O teste suplementar `display-benchmark --renderer=legacy|software|opengl` compara somente o componente de exibição, com imagem sintética RGB32 1920×1080 escalada para 640×360. Usa 5 segundos de aquecimento e 10 de medição, três repetições por renderer; não substitui a matriz de 15/60 segundos. Mede CPU total do processo, incluindo trabalho enfileirado, e FPS. `processing_p50_ms`/`processing_p95_ms` nos dados brutos medem apenas o callback de atualização/desenho e o fence `glFinish`; pintura Qt enfileirada pode ocorrer fora desse intervalo. Esses tempos não são tratados como latência de apresentação. Decoder, rede e scanout do monitor estão fora desse teste suplementar. Deve ser executado separadamente, num contexto gráfico real, após a matriz principal.

CPU por processo usa `/proc`: 100% corresponde a um núcleo, podendo exceder 100%. Memória é RSS. A atividade de GPU usa contadores DRM de tempo por engine/contexto quando disponíveis; não equivale a uma porcentagem global do chip e pode somar mais de 100%. Falta de contador será indicada como indisponível. Custos do host e dos viewers locais são separados.

O fixture copia e carimba um frame sintético por captura nas duas versões; esse custo não existe dessa forma na captura real. Descarte conhecido é medido por sobrecarga das filas de vídeo, fila de um frame completamente decodificado e substituição de imagens antes da exibição; `frames_pending_estimate` é uma diferença de contadores, não um descarte confirmado. Quando o pool NV12 de oito buffers está ocupado, o frame preparado é descartado antes de codificar, sem alternar as caps para I420; esse descarte tem contador próprio. A fila appsrc também é limitada e pode descartar sem contador disponível nas versões antigas do GStreamer. Na aplicação real, a captura registra frames substituídos antes da distribuição e tempo médio da conversão compartilhada.

No recebimento H.264, o GStreamer seleciona um decoder compatível a partir das caps reais do fluxo. A alternativa por software pode ser exercitada com `LAZARUS_VIDEO_DECODER=software`. Falha de decoder é distinta de encoder e transporte; esta etapa não promete recuperação de falhas de driver do decoder durante uma transmissão. A fila após o decoder evita acumular imagens antigas na conversão; nenhum fragmento RTP ou frame comprimido de referência é descartado por essa fila. `decoder_fps` mede a saída crua do decoder, enquanto `video_fps`/FPS do benchmark mede os frames convertidos disponíveis para exibição, mantendo o mesmo ponto de comparação da 0.2.0.

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

Testes locais cobrem módulos de bitrate, conversão/pixels, watchdog de cinco segundos, software, qualidade, vídeo sem áudio, quatro viewers, recuperação isolada, callback antigo, falha terminal, pausa/retomada e fallback TURN UDP/TCP/TLS. O [CI do código de mídia `9831e744`](https://github.com/LazaroLanderson/lazarus-share/actions/runs/37159982473) passou em Linux e Windows, incluindo inicialização dos pacotes portáteis. Linux executou os 14 testes de mídia/módulos e os testes de sessões/relay; Windows validou módulos, protocolo, perfil, falha controlada na criação do encoder, TLS e abertura do EXE em ambiente sem ferramentas de desenvolvimento. Isso não valida captura e GPU reais no Windows.

Ainda pendentes: NVIDIA real, Quick Sync no Windows, vários dispositivos de GPU, monitores 4K/HiDPI, captura física Wayland/Windows e o caso do Windows com 0 kbps. A sessão sintética local não substitui testes entre máquinas ou de longa duração na Dtel.

O bitrate informado é de vídeo RTP. VP8 usa CBR e o ajuste de velocidade `cpu-used=8`, validado após a configuração anterior perder mais de 5% de FPS em Alta com um viewer. Essa opção prioriza processamento em tempo real no encoder por software; não altera os alvos de resolução/FPS/bitrate, mas a eficiência de compressão pode depender do conteúdo; o teto é um alvo do encoder, não um limitador instantâneo de pacotes. O vídeo mantém os intervalos da captura, ancorados ao relógio de cada Peer, em vez de usar o instante variável da interface para cada frame. Timestamps ausentes ou reiniciados reinicializam essa âncora. Áudio autorizado, cabeçalhos adicionais e retransmissões podem aumentar o upload real da interface de rede. `frames_pending_estimate` não é usado para declarar descartes; a referência 0.2.0 não tinha o contador de descarte conhecido por fila e exibição da nova versão.


## Dados e repetição

O código de mídia medido é o commit `9831e744a2ccc76f6d1d8c0039e3200c209137e4`; commits posteriores desta entrega acrescentam ferramenta de exibição, documentação, verificação/empacotamento do runtime e atualização do rascunho, sem alterar esse caminho de mídia. Os hashes dos executáveis locais estão em cada registro. O binário de benchmark não é o AppImage/EXE do CI.

- [Conjunto final de 108 registros](benchmarks/0.2.1-results.jsonl): reexecutável pelo gerador de relatório, com uma única versão de binário por lado.
- [Matriz inicial completa](benchmarks/0.2.1-initial-matrix.jsonl): inclui Alta/2/software fora do limite.
- [Repetição intercalada](benchmarks/0.2.1-high2-software-retest.jsonl): seis execuções novas, 15/60 segundos, três por versão, alternando a ordem.
- [VP8 antes do ajuste de velocidade](benchmarks/0.2.1-vp8-before-speed.jsonl): evidência da regressão corrigida de Alta/1/software.
- [Exibição isolada](benchmarks/0.2.1-display.jsonl): nove execuções no contexto gráfico real, fora da matriz principal.

Dezenove medições válidas da referência foram retomadas após corrigir o candidato; as outras foram intercaladas na rodada final. Essa retomada não garante igualdade de temperatura/carga ao longo do tempo. Alta/2/software inicialmente mediu 39,06 → 35,52 FPS (queda de 9,1%); a repetição nova e intercalada mediu **36,79 → 42,29 FPS**, p95 **156 → 88 ms**, CPU **470,2% → 473,5%**. Não foi comprovada uma causa térmica ou de carga. A diferença entre rodadas mostra a variabilidade deste equipamento; três repetições não são uma garantia para qualquer PC.

O conjunto final substitui **ambos os lados**, todas as três repetições de Alta/2/software, pelo novo conjunto emparelhado. Nenhuma repetição individual foi escolhida por ser favorável. A matriz inicial continua disponível para auditoria.

O candidato anterior, com `cpu-used=6`, perdeu 8,2% de FPS em Alta/1/software. O ajuste para `cpu-used=8` recuperou 58,83 → 59,48 FPS, p95 269 → 41 ms e reduziu CPU em 19,5%. Esse parâmetro prioriza velocidade e pode mudar a eficiência de compressão por conteúdo; a validação de pixels não substitui avaliação visual de textos/jogos.

```sh
python3 scripts/report-media-benchmark.py docs/benchmarks/0.2.1-results.jsonl
# Teste suplementar, depois da matriz, em uma sessão gráfica real:
python3 scripts/benchmark-display.py
```

## Exibição no Iris Xe

Imagem sintética RGB32 1920×1080, componente 640×360; três repetições por modo, 5 segundos de aquecimento e 10 de medição. A referência reproduz a operação `QPixmap::fromImage(...).scaled(...)` da 0.2.0, com o mesmo formato de entrada, isolando o renderer. Não é uma sessão WebRTC nem comparação completa entre executáveis publicados.

| Componente | CPU do processo | FPS de atualizações |
|---|---:|---:|
| Escala antiga | 31.6% | 58.80 |
| Fallback por software | 19.8% | 58.81 |
| OpenGL | 19.3% | 58.80 |

OpenGL foi confirmado por `Mesa Intel(R) Iris(R) Xe Graphics (ADL GT2)` e reduziu CPU em **39.0%** frente à escala antiga nesse teste. O fallback raster também reduziu CPU. Não declaramos ganho de latência física de monitor/OpenGL a partir destes callbacks. A implementação evita upload sem frame novo; testes de pixels cobrem proporção, limpeza e alternativa por software.

## Comparação medida

Medianas de três repetições. FPS e latência usam o pior viewer de cada repetição; CPU dos viewers é apresentada separadamente. CPU de 100% equivale a um núcleo.

| Preset | Viewers | Backend | CPU host 0.2.0 → 0.2.1 | Ganho CPU | FPS codificados | FPS decodificados | p95 apresentação | Gate |
|---|---:|---|---|---:|---|---|---|---|
| high | 1 | hardware | 92.5% → 85.7% | +7.4% | 60.01 → 60.01 | 59.70 → 59.32 | 1195 → 41 ms | PASSOU |
| high | 1 | software | 251.4% → 202.3% | +19.5% | 58.83 → 59.48 | 58.83 → 59.48 | 269 → 41 ms | PASSOU |
| high | 2 | hardware | 191.5% → 164.9% | +13.9% | 59.13 → 59.20 | 37.81 → 48.94 | 26080 → 67 ms | PASSOU |
| high | 2 | software | 470.2% → 473.5% | -0.7% | 36.81 → 42.30 | 36.79 → 42.29 | 156 → 88 ms | PASSOU |
| high | 4 | hardware | 296.8% → 338.5% | -14.1% | 24.90 → 36.81 | 22.50 → 26.26 | 9161 → 156 ms | PASSOU |
| high | 4 | software | 712.4% → 747.6% | -4.9% | 13.81 → 15.49 | 13.79 → 15.50 | 202 → 176 ms | PASSOU |
| low | 1 | hardware | 17.5% → 18.1% | -3.2% | 30.01 → 30.01 | 30.01 → 30.01 | 55 → 19 ms | PASSOU |
| low | 1 | software | 58.7% → 56.2% | +4.2% | 30.01 → 30.01 | 30.01 → 30.01 | 24 → 23 ms | PASSOU |
| low | 2 | hardware | 35.8% → 33.7% | +5.7% | 30.01 → 30.01 | 30.01 → 30.01 | 60 → 25 ms | PASSOU |
| low | 2 | software | 161.9% → 153.9% | +4.9% | 29.97 → 29.92 | 29.97 → 29.93 | 34 → 31 ms | PASSOU |
| low | 4 | hardware | 92.4% → 72.2% | +21.9% | 29.96 → 30.01 | 29.97 → 30.01 | 72 → 32 ms | PASSOU |
| low | 4 | software | 730.2% → 667.8% | +8.6% | 26.26 → 27.30 | 26.24 → 27.36 | 142 → 133 ms | PASSOU |
| native | 1 | hardware | 86.9% → 78.7% | +9.4% | 59.98 → 59.98 | 59.92 → 59.46 | 208 → 38 ms | PASSOU |
| native | 1 | software | 238.6% → 191.8% | +19.6% | 59.44 → 59.61 | 59.48 → 59.61 | 97 → 38 ms | PASSOU |
| native | 2 | hardware | 172.7% → 143.0% | +17.2% | 59.35 → 59.88 | 44.16 → 53.50 | 18159 → 57 ms | PASSOU |
| native | 2 | software | 470.8% → 472.2% | -0.3% | 37.74 → 43.48 | 37.64 → 43.46 | 152 → 86 ms | PASSOU |
| native | 4 | hardware | 294.6% → 321.2% | -9.0% | 28.34 → 41.57 | 24.91 → 29.91 | 10210 → 140 ms | PASSOU |
| native | 4 | software | 688.6% → 727.9% | -5.7% | 11.69 → 14.29 | 11.69 → 14.32 | 227 → 185 ms | PASSOU |

## Recursos por processo

| Preset | Viewers | Backend | CPU viewers 0.2.0 → 0.2.1 | RSS host 0.2.0 → 0.2.1 | Variação RSS host 0.2.1 na janela |
|---|---:|---|---|---|---|
| high | 1 | hardware | 165.3% → 97.4% | 111.8 → 145.7 MiB | +0.12 MiB |
| high | 1 | software | 173.4% → 99.9% | 149.7 → 149.7 MiB | +0.07 MiB |
| high | 2 | hardware | 328.7% → 275.5% | 128.0 → 165.3 MiB | +0.14 MiB |
| high | 2 | software | 323.3% → 255.9% | 232.9 → 232.9 MiB | +0.04 MiB |
| high | 4 | hardware | 646.7% → 568.0% | 162.8 → 201.9 MiB | +0.30 MiB |
| high | 4 | software | 287.9% → 241.7% | 389.1 → 388.9 MiB | +0.01 MiB |
| low | 1 | hardware | 32.9% → 23.9% | 95.6 → 103.3 MiB | +1.38 MiB |
| low | 1 | software | 29.6% → 25.0% | 99.0 → 97.9 MiB | +0.00 MiB |
| low | 2 | hardware | 84.9% → 62.5% | 111.8 → 120.1 MiB | +0.07 MiB |
| low | 2 | software | 83.3% → 68.5% | 135.7 → 132.4 MiB | +0.02 MiB |
| low | 4 | hardware | 254.3% → 165.9% | 143.5 → 150.2 MiB | +0.07 MiB |
| low | 4 | software | 245.3% → 209.0% | 209.5 → 202.3 MiB | +0.02 MiB |
| native | 1 | hardware | 159.5% → 91.3% | 106.9 → 145.5 MiB | +0.12 MiB |
| native | 1 | software | 164.2% → 96.3% | 149.9 → 145.5 MiB | +0.00 MiB |
| native | 2 | hardware | 326.2% → 261.2% | 123.3 → 165.4 MiB | +0.11 MiB |
| native | 2 | software | 329.7% → 263.1% | 232.7 → 232.9 MiB | +0.00 MiB |
| native | 4 | hardware | 641.7% → 559.8% | 162.7 → 204.5 MiB | +0.19 MiB |
| native | 4 | software | 288.7% → 241.6% | 389.5 → 389.2 MiB | +0.01 MiB |

## Contadores de GPU

Tempo de engine/contexto DRM; não é utilização global do chip. Valores dos viewers são somados por repetição. Falta de contexto/contador é indisponibilidade, não zero.

| Preset | Viewers | Backend | Processo | Engine | 0.2.0 → 0.2.1 |
|---|---:|---|---|---|---|
| high | 1 | hardware | host | copy | 0.0% → 0.0% |
| high | 1 | hardware | host | render | 9.6% → 10.1% |
| high | 1 | hardware | host | video-enhance | 6.7% → 6.9% |
| high | 1 | hardware | host | video | 10.6% → 10.6% |
| high | 1 | hardware | viewers | copy | indisponível → 0.0% |
| high | 1 | hardware | viewers | render | indisponível → 0.0% |
| high | 1 | hardware | viewers | video-enhance | indisponível → 0.0% |
| high | 1 | hardware | viewers | video | indisponível → 6.7% |
| high | 2 | hardware | host | copy | 0.0% → 0.0% |
| high | 2 | hardware | host | render | 20.5% → 22.2% |
| high | 2 | hardware | host | video-enhance | 14.0% → 14.8% |
| high | 2 | hardware | host | video | 24.8% → 26.2% |
| high | 2 | hardware | viewers | copy | indisponível → 0.0% |
| high | 2 | hardware | viewers | render | indisponível → 0.0% |
| high | 2 | hardware | viewers | video-enhance | indisponível → 0.0% |
| high | 2 | hardware | viewers | video | indisponível → 14.8% |
| high | 4 | hardware | host | copy | 0.0% → 0.0% |
| high | 4 | hardware | host | render | 17.1% → 27.8% |
| high | 4 | hardware | host | video-enhance | 10.9% → 17.7% |
| high | 4 | hardware | host | video | 22.6% → 36.1% |
| high | 4 | hardware | viewers | copy | indisponível → 0.0% |
| high | 4 | hardware | viewers | render | indisponível → 0.0% |
| high | 4 | hardware | viewers | video-enhance | indisponível → 0.0% |
| high | 4 | hardware | viewers | video | indisponível → 19.1% |
| low | 1 | hardware | host | copy | 0.0% → 0.0% |
| low | 1 | hardware | host | render | 2.8% → 2.8% |
| low | 1 | hardware | host | video-enhance | 1.3% → 1.3% |
| low | 1 | hardware | host | video | 2.0% → 1.9% |
| low | 1 | hardware | viewers | copy | indisponível → 0.0% |
| low | 1 | hardware | viewers | render | indisponível → 0.0% |
| low | 1 | hardware | viewers | video-enhance | indisponível → 0.0% |
| low | 1 | hardware | viewers | video | indisponível → 1.3% |
| low | 2 | hardware | host | copy | 0.0% → 0.0% |
| low | 2 | hardware | host | render | 6.6% → 6.6% |
| low | 2 | hardware | host | video-enhance | 3.1% → 3.0% |
| low | 2 | hardware | host | video | 4.6% → 4.6% |
| low | 2 | hardware | viewers | copy | indisponível → 0.0% |
| low | 2 | hardware | viewers | render | indisponível → 0.0% |
| low | 2 | hardware | viewers | video-enhance | indisponível → 0.0% |
| low | 2 | hardware | viewers | video | indisponível → 3.0% |
| low | 4 | hardware | host | copy | 0.0% → 0.0% |
| low | 4 | hardware | host | render | 12.4% → 13.0% |
| low | 4 | hardware | host | video-enhance | 5.8% → 6.0% |
| low | 4 | hardware | host | video | 9.5% → 9.9% |
| low | 4 | hardware | viewers | copy | indisponível → 0.0% |
| low | 4 | hardware | viewers | render | indisponível → 0.0% |
| low | 4 | hardware | viewers | video-enhance | indisponível → 0.0% |
| low | 4 | hardware | viewers | video | indisponível → 6.1% |
| native | 1 | hardware | host | copy | 0.0% → 0.0% |
| native | 1 | hardware | host | render | 9.9% → 10.1% |
| native | 1 | hardware | host | video-enhance | 6.8% → 6.8% |
| native | 1 | hardware | host | video | 11.3% → 11.0% |
| native | 1 | hardware | viewers | copy | indisponível → 0.0% |
| native | 1 | hardware | viewers | render | indisponível → 0.0% |
| native | 1 | hardware | viewers | video-enhance | indisponível → 0.0% |
| native | 1 | hardware | viewers | video | indisponível → 6.7% |
| native | 2 | hardware | host | copy | 0.0% → 0.0% |
| native | 2 | hardware | host | render | 19.8% → 20.2% |
| native | 2 | hardware | host | video-enhance | 12.9% → 13.4% |
| native | 2 | hardware | host | video | 23.8% → 23.6% |
| native | 2 | hardware | viewers | copy | indisponível → 0.0% |
| native | 2 | hardware | viewers | render | indisponível → 0.0% |
| native | 2 | hardware | viewers | video-enhance | indisponível → 0.0% |
| native | 2 | hardware | viewers | video | indisponível → 13.4% |
| native | 4 | hardware | host | copy | 0.0% → 0.0% |
| native | 4 | hardware | host | render | 20.6% → 33.3% |
| native | 4 | hardware | host | video-enhance | 13.0% → 21.1% |
| native | 4 | hardware | host | video | 27.7% → 43.0% |
| native | 4 | hardware | viewers | copy | indisponível → 0.0% |
| native | 4 | hardware | viewers | render | indisponível → 0.0% |
| native | 4 | hardware | viewers | video-enhance | indisponível → 0.0% |
| native | 4 | hardware | viewers | video | indisponível → 22.6% |

## CPU por frame codificado e decoder

Normalização complementar; não substitui a meta de custo total do host. Uma versão que produz mais frames pode consumir mais CPU total.

| Preset | Viewers | Backend | CPU host por frame 0.2.0 → 0.2.1 | Decoder 0.2.0 → 0.2.1 |
|---|---:|---|---|---|
| high | 1 | hardware | 15.47 → 14.29 ms de CPU | openh264dec → vah264dec |
| high | 1 | software | 42.74 → 34.04 ms de CPU | vp8dec → vp8dec |
| high | 2 | hardware | 16.05 → 13.92 ms de CPU | openh264dec → vah264dec |
| high | 2 | software | 63.81 → 55.99 ms de CPU | vp8dec → vp8dec |
| high | 4 | hardware | 29.43 → 22.90 ms de CPU | openh264dec → vah264dec |
| high | 4 | software | 126.31 → 117.75 ms de CPU | vp8dec → vp8dec |
| low | 1 | hardware | 5.83 → 6.02 ms de CPU | openh264dec → vah264dec |
| low | 1 | software | 19.57 → 18.73 ms de CPU | vp8dec → vp8dec |
| low | 2 | hardware | 5.96 → 5.62 ms de CPU | openh264dec → vah264dec |
| low | 2 | software | 26.97 → 25.68 ms de CPU | vp8dec → vp8dec |
| low | 4 | hardware | 7.72 → 6.01 ms de CPU | openh264dec → vah264dec |
| low | 4 | software | 68.93 → 60.19 ms de CPU | vp8dec → vp8dec |
| native | 1 | hardware | 14.52 → 13.13 ms de CPU | openh264dec → vah264dec |
| native | 1 | software | 40.13 → 32.17 ms de CPU | vp8dec → vp8dec |
| native | 2 | hardware | 14.51 → 11.92 ms de CPU | openh264dec → vah264dec |
| native | 2 | software | 62.10 → 54.30 ms de CPU | vp8dec → vp8dec |
| native | 4 | hardware | 25.43 → 19.01 ms de CPU | openh264dec → vah264dec |
| native | 4 | software | 141.76 → 125.33 ms de CPU | vp8dec → vp8dec |

Cenários completos: 18/18. Incompletos: 0. Gates para investigar: 0. Hardware indisponível: 0.

## Captura, vídeo e descartes do candidato

Medianas das três repetições. Bitrate soma somente vídeo RTP dos viewers, sem áudio. Descartes conhecidos são somados por conexão, por amostra de aproximadamente dois segundos; o contador equivalente não existia na referência. Frames perdidos por filas sem contador não são inventados.

| Preset | Viewers | Encoder | Captura FPS | Decoder bruto FPS (pior viewer) | Vídeo RTP total | Descarte antes de codificar/amostra | Descarte na exibição/amostra | Descarte no pool NV12 |
|---|---:|---|---:|---:|---:|---:|---:|---:|
| high | 1 | hardware | 60.00 | 60.00 | 8026 kbps | 0.03 | 1.40 | 0 |
| high | 1 | software | 60.00 | 59.48 | 8909 kbps | 1.07 | 0.67 | 0 |
| high | 2 | hardware | 59.97 | 59.20 | 15847 kbps | 2.97 | 40.77 | 0 |
| high | 2 | software | 60.00 | 42.29 | 22990 kbps | 70.80 | 0.00 | 0 |
| high | 4 | hardware | 58.40 | 36.86 | 19887 kbps | 171.80 | 84.73 | 0 |
| high | 4 | software | 59.50 | 15.50 | 33109 kbps | 346.33 | 0.00 | 0 |
| low | 1 | hardware | 30.00 | 30.01 | 2958 kbps | 0.00 | 0.00 | 0 |
| low | 1 | software | 30.00 | 30.01 | 3009 kbps | 0.00 | 0.00 | 0 |
| low | 2 | hardware | 30.00 | 30.01 | 5917 kbps | 0.00 | 0.00 | 0 |
| low | 2 | software | 30.00 | 29.93 | 6118 kbps | 0.20 | 0.00 | 0 |
| low | 4 | hardware | 30.00 | 30.01 | 11834 kbps | 0.00 | 0.00 | 0 |
| low | 4 | software | 30.00 | 27.36 | 15227 kbps | 18.33 | 0.10 | 0 |
| native | 1 | hardware | 60.00 | 59.98 | 8023 kbps | 0.07 | 1.03 | 0 |
| native | 1 | software | 60.00 | 59.61 | 8936 kbps | 0.80 | 0.63 | 0 |
| native | 2 | hardware | 60.00 | 59.88 | 16026 kbps | 0.40 | 25.20 | 0 |
| native | 2 | software | 60.00 | 43.48 | 23490 kbps | 66.20 | 0.00 | 0 |
| native | 4 | hardware | 59.13 | 41.70 | 22703 kbps | 134.97 | 97.30 | 0 |
| native | 4 | software | 59.40 | 14.32 | 32947 kbps | 356.70 | 0.00 | 0 |

Filas foram verificadas por testes: pool NV12 limitado a oito buffers, fila de imagem crua após decoder limitada a um, filas de envio limitadas e descarte antes da codificação. O teste de exibição lenta confirmou descarte de imagens completas, sem corromper referências H.264. RSS e variação durante 60 segundos estão nas tabelas de recursos e nos dados por processo. Essa janela não comprova ausência de vazamentos em sessões de várias horas. Tempos de preparação/seleção, motivo de fallback e alterações de bitrate aparecem nos eventos estruturados locais; não habilitamos logs brutos nem exportação automática.

A validação dos pacotes exige `decodebin` e também decodifica vídeo H.264 sintético com os plugins empacotados, sem hardware/rede/perfil. Isso detecta dependências ausentes do autoplugging, além de verificar que as fábricas foram registradas. O caminho da interface e os controles de compartilhamento não são acionados nesse teste.
