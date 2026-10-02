#!/usr/bin/env bash
# Run through SSH, or locally on the VPS: bash infra/deploy.sh COMMIT DIRECTORY MODE
set -Eeuo pipefail
revision=${1:?commit SHA required}
checkout=${2:?checkout directory required}
mode=${3:-full}
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
[[ -f infra/.env ]] || { echo 'Missing private infra/.env on VPS' >&2; exit 1; }
[[ -z $(git status --porcelain --untracked-files=no) ]] || { echo 'Tracked VPS files have local changes' >&2; exit 1; }
git fetch --no-tags origin main
git merge-base --is-ancestor "$revision" origin/main || { echo 'Commit is not on origin/main' >&2; exit 1; }
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
if [[ -z $host ]] || ! curl --fail --silent --show-error --retry 5 --retry-delay 2 --max-time 10 "https://$host/health" >/dev/null; then
  rollback; exit 1
fi
printf '%s\n' "$revision" > .deploy/current
printf '%s\n' "$previous" > .deploy/previous
echo "Deployment healthy: $revision ($mode)"
