# Componentes de terceiros

O código próprio é GPL-3.0-only. Qt 6 usa sua opção aberta GPL/LGPL; GStreamer
usa LGPL-2.1-or-later, com plugins e dependências possuindo licenças próprias.
PipeWire e libvpx são permissivos; libnice usa MPL/LGPL; Opus usa BSD.

`docs/licenses/notices` conserva avisos dos pacotes utilizados e
`docs/licenses` inclui textos de licenças copyleft. O empacotamento Windows
também copia avisos distribuídos pelos pacotes MSYS2. O empacotamento Linux
copia os avisos disponíveis na distribuição para as bibliotecas e plugins efetivamente incluídos.

Fontes e instruções de build dos componentes principais:

- Qt: https://code.qt.io/
- GStreamer: https://gitlab.freedesktop.org/gstreamer/gstreamer
- libnice: https://gitlab.freedesktop.org/libnice/libnice
- PipeWire: https://gitlab.freedesktop.org/pipewire/pipewire
- libvpx: https://chromium.googlesource.com/webm/libvpx/
- Opus: https://gitlab.xiph.org/xiph/opus
- AppImage runtime: https://github.com/AppImage/type2-runtime

Antes de publicar binários, disponibilizar o código correspondente e as
instruções de compilação, incluindo versões/patches das bibliotecas copyleft
efetivamente distribuídas. Links upstream e avisos, sozinhos, não substituem
obrigações de fornecimento das fontes correspondentes. Não é feita publicação
pública automática por este projeto.
