#!/usr/bin/env bash
set -Eeuo pipefail
host=${1:?domain required}
[[ $host =~ ^[A-Za-z0-9.-]+$ ]]
cd /opt/lazarus-share
umask 077
exec 9>.deploy/lock
flock -w 300 9
before=$(sha256sum infra/certs/public/fullchain.pem)
docker run --rm -v "$PWD/infra/acme:/etc/letsencrypt" -v "$PWD/infra/acme-webroot:/acme" certbot/certbot:v5.0.0 renew --webroot -w /acme --quiet
install -m 644 "infra/acme/live/$host/fullchain.pem" infra/certs/public/fullchain.pem
install -m 600 "infra/acme/live/$host/privkey.pem" infra/certs/public/privkey.pem
if [[ $(sha256sum infra/certs/public/fullchain.pem) != "$before" ]]; then
    docker compose --project-name lazarus-share --env-file infra/.env -f infra/compose.single.yml exec -T proxy nginx -s reload
    docker compose --project-name lazarus-share --env-file infra/.env -f infra/compose.single.yml restart turn
fi
