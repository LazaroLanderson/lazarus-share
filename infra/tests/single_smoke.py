"""Isolated real Nginx/domain-SNI and Coturn smoke for single-IP deployment."""
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import tempfile
import time

SOURCE=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='lazarus-single-smoke-') as directory:
    root=Path(directory);infra=root/'infra';infra.mkdir();(root/'server').mkdir()
    for name in ('compose.single.yml','nginx.single.conf.template','Dockerfile'):
        shutil.copy2(SOURCE/'infra'/name,infra/name)
    for name in ('service.py','requirements.txt'):shutil.copy2(SOURCE/'server'/name,root/'server'/name)
    for name in ('signal','public'):
        certs=infra/'certs'/name;certs.mkdir(parents=True)
        subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost,IP:127.0.0.2','-keyout',str(certs/'privkey.pem'),'-out',str(certs/'fullchain.pem')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    (infra/'acme-webroot').mkdir()
    (infra/'.env').write_text('SIGNAL_HOST=localhost\nSIGNAL_IP=127.0.0.2\nTURN_PRIVATE_IP=127.0.0.2\nTURN_PUBLIC_IP=127.0.0.2\nTURN_SECRET=isolated-smoke-test-only\n')
    compose=['docker','compose','--project-name','lazarus-single-smoke','--env-file',str(infra/'.env'),'-f',str(infra/'compose.single.yml')]
    try:
        subprocess.run(compose+['up','-d','--build','--wait','--wait-timeout','120'],check=True)
        # The SNI/domain certificate must be public, not the old default IP certificate.
        context=ssl.create_default_context(cafile=str(infra/'certs/public/fullchain.pem'))
        with socket.create_connection(('127.0.0.2',443),timeout=5) as connection:
            with context.wrap_socket(connection,server_hostname='localhost') as tls:
                tls.sendall(b'GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')
                if b'200 OK' not in tls.recv(4096):raise SystemExit('Domain proxy health failed')
        for attempt in range(100):
            try:
                with socket.create_connection(('127.0.0.2',5349),timeout=.2):pass
                break
            except OSError:time.sleep(.1)
        with socket.create_connection(('127.0.0.2',5349),timeout=5) as connection:
            with context.wrap_socket(connection,server_hostname='localhost'):pass
        subprocess.run([os.environ.get('PYTHON','python3'),str(SOURCE/'infra/stun_probe.py'),'--host','127.0.0.2'],check=True)
        print('Single-IP domain SNI, proxy health, TURN TLS and STUN passed')
    finally:
        subprocess.run(compose+['down','--remove-orphans'],check=False,stdout=subprocess.DEVNULL)
