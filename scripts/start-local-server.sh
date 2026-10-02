#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT"
if [ ! -x .venv/bin/python ]; then
    echo 'Prepare as dependências do servidor conforme README.md.' >&2
    exit 1
fi
exec .venv/bin/python server/local.py "$@"
