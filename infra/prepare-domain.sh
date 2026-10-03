#!/usr/bin/env bash
# Run on the VPS as the deploy user. Does not replace the running service.
set -Eeuo pipefail
host=${1:?domain required}
public=${2:?public IPv4 required}
[[ $host =~ ^[a-zA-Z0-9.-]+$ && $public =~ ^[0-9.]+$ ]]
cd /opt/lazarus-share
python3 - "$host" "$public" <<'PY'
import socket,sys
host,address=sys.argv[1:]
values={item[4][0] for item in socket.getaddrinfo(host,80,type=socket.SOCK_STREAM)}
if address not in values: raise SystemExit('DNS does not point to this VPS; existing service preserved')
PY
if sudo -n ss -H -ltn 'sport = :80' | grep -q .; then echo 'Port 80 is occupied; existing services preserved' >&2; exit 1; fi
umask 077
mkdir -p infra/acme infra/acme-webroot infra/certs/public
# Bootstrap HTTP-01 independently, while the old HTTPS service remains running.
docker run --rm -p 80:80 -v "$PWD/infra/acme:/etc/letsencrypt" certbot/certbot:v5.0.0 certonly --standalone --non-interactive --agree-tos --register-unsafely-without-email -d "$host"
sudo -n install -m 644 "infra/acme/live/$host/fullchain.pem" infra/certs/public/fullchain.pem
sudo -n install -m 600 "infra/acme/live/$host/privkey.pem" infra/certs/public/privkey.pem
python3 - "$host" "$public" <<'PY'
import os,secrets,socket,sys,tempfile
from pathlib import Path
host,public=sys.argv[1:]
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
    s.connect(('169.254.169.254',80)); private=s.getsockname()[0]
p=Path('infra/.env'); values=dict(line.split('=',1) for line in p.read_text().splitlines() if '=' in line and not line.lstrip().startswith('#'))
values.update(SIGNAL_HOST=host,SIGNAL_IP='0.0.0.0',SIGNAL_TLS_MODE='system',TURN_HOST=host,TURN_PUBLIC_IP=public,TURN_PRIVATE_IP=private)
if not values.get('TURN_SECRET'): values['TURN_SECRET']=secrets.token_hex(32)
with tempfile.NamedTemporaryFile(mode='w',dir=p.parent,delete=False) as f:
    f.write(''.join(k+'='+v+'\n' for k,v in values.items())); temporary=f.name
os.replace(temporary,p)
PY
sudo -n tee /etc/systemd/system/lazarus-share-cert-renew.service >/dev/null <<UNIT
[Unit]
Description=Renew Lazarus Share public TLS certificate
[Service]
Type=oneshot
WorkingDirectory=/opt/lazarus-share
ExecStart=/bin/bash /opt/lazarus-share/infra/renew-domain.sh $host
UNIT
sudo -n tee /etc/systemd/system/lazarus-share-cert-renew.timer >/dev/null <<'UNIT'
[Unit]
Description=Check Lazarus Share certificate twice daily
[Timer]
OnCalendar=*-*-* 03,15:00:00
RandomizedDelaySec=1800
Persistent=true
[Install]
WantedBy=timers.target
UNIT
sudo -n systemctl daemon-reload
sudo -n systemctl enable --now lazarus-share-cert-renew.timer
echo 'Domain certificate and private TURN configuration prepared'
