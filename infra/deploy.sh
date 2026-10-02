#!/usr/bin/env bash
# Run through SSH, or locally on the VPS: bash infra/deploy.sh COMMIT DIRECTORY MODE [SIGNAL_HOST SIGNAL_IP SIGNAL_TLS_MODE]
set -Eeuo pipefail
revision=${1:?commit SHA required}
checkout=${2:?checkout directory required}
mode=${3:-full}
signal_host=${4:-}
signal_ip=${5:-0.0.0.0}
signal_tls_mode=${6:-system}
[[ "$signal_tls_mode" == system || "$signal_tls_mode" == pinned ]] || { echo "Invalid TLS verification mode" >&2; exit 2; }
[[ $revision =~ ^[0-9a-f]{40}$ ]] || { echo 'Invalid commit SHA' >&2; exit 2; }
[[ $checkout == /* && -d $checkout/.git ]] || { echo 'Prepare the VPS checkout first' >&2; exit 2; }
case "$mode" in
  full) config=infra/compose.yml ;;
  signaling) config=infra/compose.signaling.yml ;;
  *) echo 'Mode must be full or signaling' >&2; exit 2 ;;
esac
cd "$checkout"
mkdir -p .deploy
exec 9>.deploy/lock
flock -w 300 9 || { echo 'Another deployment is still running' >&2; exit 1; }
[[ -z $(git status --porcelain --untracked-files=no) ]] || { echo 'Tracked VPS files have local changes' >&2; exit 1; }
git fetch --no-tags origin main
git merge-base --is-ancestor "$revision" origin/main || { echo 'Commit is not on origin/main' >&2; exit 1; }
# Public GitHub Variables configure only the single-IP/P2P mode.
# The TURN secret and other private configuration are preserved on the VPS.
if [[ "$mode" == signaling && -n "$signal_host" && "$signal_host" != - ]]; then
  python3 - "$signal_host" "$signal_ip" "$signal_tls_mode" <<'PYENV'
import ipaddress
import os
from pathlib import Path
import re
import sys
import tempfile
host, address, tls_mode = sys.argv[1:]
if len(host) > 253 or not all(re.fullmatch(r'[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?', label) for label in host.split('.')):
    raise SystemExit('SIGNAL_HOST must be a valid hostname without scheme/path')
ipaddress.IPv4Address(address)
path = Path('infra/.env')
lines = path.read_text().splitlines() if path.exists() else []
lines = [line for line in lines if not re.match(r'^\s*(?:export\s+)?SIGNAL_(HOST|IP|TLS_MODE)\s*=', line)]
lines += ['SIGNAL_HOST=' + host, 'SIGNAL_IP=' + address, 'SIGNAL_TLS_MODE=' + tls_mode]
with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False) as output:
    output.write('\n'.join(lines) + '\n')
    temporary = output.name
try:
    os.replace(temporary, path)
finally:
    if os.path.exists(temporary): os.unlink(temporary)
PYENV
fi
[[ -f infra/.env ]] || { echo 'Missing private infra/.env on VPS' >&2; exit 1; }
# Check TLS and the public health route using SIGNAL_HOST from Compose's environment file.
host=$(python3 - <<'PY'
from pathlib import Path
import re
for line in Path('infra/.env').read_text().splitlines():
    if line.startswith('SIGNAL_HOST='):
        value=line.partition('=')[2].strip().strip('\"\'')
        if re.fullmatch(r'[A-Za-z0-9.-]+',value): print(value)
        break
PY
)
[[ -n $host ]] || { echo "Set SIGNAL_HOST in infra/.env" >&2; exit 1; }
readarray -t tls_settings < <(python3 - <<'PYTLS'
from pathlib import Path
values = dict(line.split('=', 1) for line in Path('infra/.env').read_text().splitlines() if '=' in line and not line.lstrip().startswith('#'))
print(values.get('SIGNAL_TLS_MODE', 'system').strip().strip('\"\''))
print(values.get('SIGNAL_IP', '0.0.0.0').strip().strip('\"\''))
PYTLS
)
verification=${tls_settings[0]:-system}
[[ "$verification" == system || "$verification" == pinned ]] || { echo 'Invalid SIGNAL_TLS_MODE in .env' >&2; exit 1; }
health_options=()
if [[ "$verification" == pinned ]]; then
  [[ -r infra/certs/signal/fullchain.pem ]] || { echo 'Missing pinned TLS certificate'; exit 1; }
  health_options+=(--cacert infra/certs/signal/fullchain.pem)
fi
if [[ "$mode" == signaling ]]; then
  binding=${tls_settings[1]:-0.0.0.0}
  [[ "$binding" == 0.0.0.0 ]] && binding=127.0.0.1
  # Keep TLS hostname verification but avoid VPS public-IP/NAT hairpin routing.
  health_options+=(--connect-to "$host:443:$binding:443")
fi
previous=$(git rev-parse HEAD)
compose=(docker compose --project-name lazarus-share --env-file infra/.env -f "$config")
rollback() {
  echo 'Deployment failed; restoring previous commit' >&2
  git checkout --detach "$previous"
  "${compose[@]}" up -d --build --force-recreate --remove-orphans --wait --wait-timeout 120 || {
    echo 'Automatic recovery failed; operator intervention required' >&2; return 1;
  }
}
git checkout --detach "$revision"
# Quiet validation avoids printing secrets from the resolved Compose configuration.
if ! "${compose[@]}" config --quiet; then
  git checkout --detach "$previous"; exit 1
fi
# Build first, so a compilation/download failure leaves active containers intact.
if ! "${compose[@]}" build signaling; then
  git checkout --detach "$previous"; exit 1
fi
if ! "${compose[@]}" up -d --force-recreate --remove-orphans --wait --wait-timeout 120; then
  rollback; exit 1
fi
if [[ -z $host ]] || ! curl "${health_options[@]}" --fail --silent --show-error --retry 5 --retry-delay 2 --max-time 10 "https://$host/health" >/dev/null; then
  rollback; exit 1
fi
printf '%s\n' "$revision" > .deploy/current
printf '%s\n' "$previous" > .deploy/previous
echo "Deployment healthy: $revision ($mode)"
