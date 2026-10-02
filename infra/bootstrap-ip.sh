#!/usr/bin/env bash
# Temporary HTTPS for an IPv4 endpoint. Private key stays on the VPS.
set -Eeuo pipefail
address=${1:?public IPv4 required}
python3 - "$address" <<'PY'
import ipaddress,sys
ipaddress.IPv4Address(sys.argv[1])
PY
mkdir -p infra/certs/signal
chmod 700 infra/certs infra/certs/signal
cert=infra/certs/signal/fullchain.pem
key=infra/certs/signal/privkey.pem
if [[ -f $cert || -f $key ]]; then
  [[ -f $cert && -f $key ]] || { echo 'Incomplete existing TLS files; refusing to overwrite'; exit 1; }
  openssl verify -CAfile "$cert" -verify_ip "$address" "$cert" >/dev/null
  openssl x509 -in "$cert" -noout -checkend 86400 >/dev/null
else
  temporary=$(mktemp -d infra/certs/signal/.temporary.XXXXXX)
  trap 'rm -rf "$temporary"' EXIT
  openssl req -x509 -newkey rsa:2048 -nodes -days 30 \
    -subj "/CN=$address" -addext "subjectAltName=IP:$address" \
    -keyout "$temporary/privkey.pem" -out "$temporary/fullchain.pem" 2>/dev/null
  chmod 600 "$temporary/privkey.pem"
  chmod 644 "$temporary/fullchain.pem"
  mv "$temporary/privkey.pem" "$key"
  mv "$temporary/fullchain.pem" "$cert"
fi
openssl x509 -in "$cert" -noout -fingerprint -sha256
openssl x509 -in "$cert" -noout -enddate
