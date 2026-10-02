"""Local TLS signaling for a LAN test; no TURN, room persistence or access logs."""
import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import ssl
import subprocess
from aiohttp import web
from service import Service, application


def certificate(directory, addresses):
    directory.mkdir(parents=True, exist_ok=True)
    directory.chmod(0o700)
    cert, key = directory / 'certificate.pem', directory / 'private-key.pem'
    names = ','.join(['DNS:localhost', 'IP:127.0.0.1', *['IP:' + str(ipaddress.ip_address(a)) for a in addresses]])
    config = directory / 'certificate-addresses.txt'
    if not cert.exists() or not key.exists() or not config.exists() or config.read_text() != names:
        old_mask = os.umask(0o077)
        try:
            subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:3072', '-nodes', '-days', '365',
                            '-keyout', str(key), '-out', str(cert), '-subj', '/CN=Lazarus Share local',
                            '-addext', 'subjectAltName=' + names], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            config.write_text(names)
        finally:
            os.umask(old_mask)
    der = ssl.PEM_cert_to_DER_cert(cert.read_text())
    return cert, key, hashlib.sha256(der).hexdigest()


def main():
    parser = argparse.ArgumentParser(description='Servidor local criptografado para teste entre PCs')
    parser.add_argument('--bind', default='127.0.0.1', help='IP da interface local; não expor na internet')
    parser.add_argument('--port', type=int, default=8443)
    parser.add_argument('--state-dir', default='.local-server')
    args = parser.parse_args()
    address = ipaddress.ip_address(args.bind)
    if not address.is_private or address.is_unspecified or address.is_multicast:
        parser.error('Use um endereço local privado da interface, como 192.168.1.25')
    cert, key, pin = certificate(Path(args.state_dir).resolve(), [str(address)])
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert, key)
    host = '[' + str(address) + ']' if address.version == 6 else str(address)
    connection = {'server': f'wss://{host}:{args.port}/ws', 'certificate_sha256': pin, 'stun': ''}
    Path(args.state_dir, 'connection.json').write_text(json.dumps(connection, indent=2) + '\n')
    print('Servidor local pronto para iniciar. Nos dois PCs, configure:', flush=True)
    print('Servidor de salas: ' + connection['server'], flush=True)
    print('Certificado local (SHA-256): ' + pin, flush=True)
    print('STUN: deixe vazio para o teste na mesma LAN. Sem relay nesta configuração.', flush=True)
    print('Ctrl+C encerra o servidor e apaga as salas em memória.', flush=True)
    web.run_app(application(Service()), host=str(address), port=args.port, ssl_context=context, access_log=None, print=None)


if __name__ == '__main__':
    main()
