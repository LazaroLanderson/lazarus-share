#!/usr/bin/env bash
# Manual diagnostic: no viewers, no screen recording, no frame arguments in the trace.
set -euo pipefail
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
APPIMAGE="$ROOT/dist/LazarusShare-x86_64.AppImage"
DEBUGGER=/tmp/lazarus-build-deps/root/usr/bin/gdb
if [[ ! -x "$DEBUGGER" ]]; then
    DEBUGGER="$(command -v gdb || true)"
fi
if [[ -z "$DEBUGGER" ]]; then
    echo 'Depurador não encontrado. Este roteiro usa o depurador preparado nesta máquina ou um gdb existente.' >&2
    exit 1
fi
step() { printf '\n>>> %s\n' "$1"; read -r -p '    [Enter para continuar] ' _; }
capture() { local answer; printf '\n>>> %s\n' "$2"; read -r -p '    > ' answer; printf -v "$1" '%s' "$answer"; }
step 'Feche as outras janelas Lazarus Share. O teste abre o app; clique Criar sala e selecione o monitor. Se não fechar sozinho, encerre-o depois do teste. Não entre com nenhum viewer.'
DIRECTORY="$(mktemp -d /tmp/lazarus-capture-diagnostic-XXXXXX)"
trap 'rm -rf -- "$DIRECTORY"' EXIT
(cd "$DIRECTORY" && "$APPIMAGE" --appimage-extract >/dev/null)
HERE="$DIRECTORY/squashfs-root"
export APPIMAGE APPDIR="$HERE"
export LD_LIBRARY_PATH="$HERE/usr/lib:/tmp/lazarus-build-deps/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="$HERE/usr/lib/gstreamer-1.0"
export GST_PLUGIN_SCANNER_1_0="$HERE/usr/libexec/gst-plugin-scanner" GST_DEBUG=0
export PIPEWIRE_MODULE_DIR="$HERE/usr/lib/pipewire-0.3" SPA_PLUGIN_DIR="$HERE/usr/lib/spa-0.2"
export GST_REGISTRY_1_0="$DIRECTORY/registry.bin"
if [[ -f "$ROOT/.local-server/connection.json" ]]; then
    LAZARUS_SIGNAL_URL="$("$ROOT/.venv/bin/python" -c 'import json,sys; print(json.load(open(sys.argv[1]))["server"])' "$ROOT/.local-server/connection.json")"
    LAZARUS_TLS_PIN="$("$ROOT/.venv/bin/python" -c 'import json,sys; print(json.load(open(sys.argv[1]))["certificate_sha256"])' "$ROOT/.local-server/connection.json")"
    export LAZARUS_SIGNAL_URL LAZARUS_TLS_PIN
fi
ulimit -c 0
set +e
"$DEBUGGER" -nx --batch --return-child-result \
    -ex 'set debuginfod enabled off' -ex 'set print frame-arguments none' \
    -ex 'set print thread-events off' -ex 'handle SIGPIPE nostop noprint pass' \
    -ex run -ex 'python import gdb; gdb.execute("bt 20") if gdb.selected_thread() else print("Aplicativo encerrado normalmente")' \
    --args "$HERE/usr/bin/lazarus-share" 2>&1 | tee "$ROOT/dist/diagnostico-captura.txt"
RESULT=${PIPESTATUS[0]}
set -e
capture STAGE 'O fechamento ocorreu antes da seleção, depois de selecionar o monitor, ou o app permaneceu aberto? Não inclua o token da sala.'
printf '\nEXIT=%s\nETAPA=%s\n' "$RESULT" "$STAGE" | tee -a "$ROOT/dist/diagnostico-captura.txt"
echo 'Diagnóstico manual salvo em dist/diagnostico-captura.txt.'
