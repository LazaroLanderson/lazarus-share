"""Local expiry/refresh regression; no VPS access, capture or raw TURN logs.

Set LAZARUS_TURN_DOCKER_IMAGE to the image digest from infra/compose.single.yml
for the release gate. Without it, uses LAZARUS_TURNSERVER or the local binary.
"""
import base64
import hashlib
import hmac
import os
from pathlib import Path
import secrets
import runpy
import struct
import shutil
import socket
import subprocess
import tempfile
import time


def port():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        return listener.getsockname()[1]


def verify_allocation(port_number, username, password, *, expired=False):
    # Reuse the tested STUN framing helper without changing infrastructure code.
    framing = runpy.run_path('infra/tests/turn_allocations.py')
    attribute, exchange = framing['attribute'], framing['exchange']
    with socket.create_connection(('127.0.0.1', port_number), timeout=3) as connection:
        requested = attribute(0x19, b'\x11\0\0\0')
        kind, challenge = exchange(connection, requested)
        if kind != 0x113 or 0x14 not in challenge or 0x15 not in challenge:
            raise SystemExit('Expected TURN authentication challenge')
        realm, nonce = challenge[0x14], challenge[0x15]
        key = hashlib.md5(username.encode() + b':' + realm + b':' + password.encode()).digest()
        body = requested + attribute(6, username.encode()) + attribute(0x14, realm) + attribute(0x15, nonce)
        kind, response = exchange(connection, body, key)
        if expired:
            error = response.get(9, b'\0\0\0\0')
            code = (error[2] & 7) * 100 + error[3]
            if kind != 0x113 or code != 401:
                raise SystemExit('Expired credentials were accepted for a new allocation')
        elif kind != 0x103 or 0x0d not in response or not 0 < struct.unpack('!I', response[0x0d])[0] <= 6:
            raise SystemExit('Fixture did not grant the requested short allocation lifetime')


with tempfile.TemporaryDirectory(prefix='lazarus-renewal-') as directory:
    udp, tls = port(), port()
    cert, key = Path(directory, 'cert.pem'), Path(directory, 'key.pem')
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                    '-subj', '/CN=localhost', '-keyout', str(key), '-out', str(cert)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    secret = secrets.token_hex(32)
    image = os.getenv('LAZARUS_TURN_DOCKER_IMAGE')
    name = 'lazarus-renewal-' + secrets.token_hex(6)
    cert_root = '/fixture' if image else directory
    args = ['-n', '--no-cli', '--allow-loopback-peers',
            '--listening-ip=127.0.0.1', '--relay-ip=127.0.0.1',
            f'--listening-port={udp}', f'--tls-listening-port={tls}',
            '--cert=' + cert_root + '/cert.pem', '--pkey=' + cert_root + '/key.pem',
            '--min-port=55000', '--max-port=55040', '--use-auth-secret',
            '--static-auth-secret=' + secret, '--realm=lazarus.test',
            '--max-allocate-lifetime=6', '--log-file=/dev/null', '--no-stdout-log',
            '--pidfile=/tmp/lazarus-renewal.pid' if image else '--pidfile=' + directory + '/turn.pid']
    if image:
        command = ['docker', 'run', '--rm', '--name', name, '--network=host', '--user=0:0',
                   '-v', directory + ':/fixture:ro', '--entrypoint=turnserver', image, *args]
    else:
        binary = os.getenv('LAZARUS_TURNSERVER') or shutil.which('turnserver')
        if not binary:
            raise SystemExit('Install Coturn or set LAZARUS_TURNSERVER')
        command = [binary, *args]
    errors = tempfile.TemporaryFile()
    server = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=errors)
    try:
        for _ in range(100):
            if server.poll() is not None:
                errors.seek(0)
                detail = errors.read().decode(errors='replace').replace(secret, '[redacted]')
                raise SystemExit('Isolated Coturn failed to start: ' + detail[:1000])
            try:
                with socket.create_connection(('127.0.0.1', udp), timeout=.1):
                    break
            except OSError:
                time.sleep(.1)
        else:
            raise SystemExit('Isolated Coturn did not become ready')
        for scheme, transport in [('turn', ''), ('turn', '?transport=tcp'), ('turns', '')]:
            # New credential per scenario. No credential is logged or persisted.
            username = f'{int(time.time()) + 12}:{secrets.token_hex(8)}'
            password = base64.b64encode(hmac.new(secret.encode(), username.encode(), hashlib.sha1).digest()).decode()
            verify_allocation(udp, username, password)
            from urllib.parse import quote
            uri = f'{scheme}://{quote(username, safe="")}:{quote(password, safe="")}@127.0.0.1:{tls if scheme == "turns" else udp}{transport}'
            try:
                result = subprocess.run(['build/media-test', '--turn=' + uri, '--credential-lifetime=12',
                                         '--relay-duration=40'], timeout=50)
            except subprocess.TimeoutExpired:
                raise SystemExit('Relay expiry test timed out') from None
            if result.returncode:
                raise SystemExit('Relay expiry test failed; credentials omitted')
            verify_allocation(udp, username, password, expired=True)
            print(f'{scheme} {transport or "default"}: allocation refreshed beyond credential expiry', flush=True)
    finally:
        if image:
            subprocess.run(['docker', 'stop', name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
