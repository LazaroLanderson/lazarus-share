"""Verify the local TLS server and exact certificate trust through real app windows."""
import hashlib
import os
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import urllib.request

with tempfile.TemporaryDirectory(prefix='lazarus-local-tls-') as directory:
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
    env = os.environ | {'QT_QPA_PLATFORM': 'offscreen', 'PIPEWIRE_REMOTE': 'lazarus-nonexistent-test',
                        'LAZARUS_SIGNAL_URL': f'wss://127.0.0.1:{port}/ws'}
    server = subprocess.Popen([sys.executable, 'server/local.py', '--port', str(port), '--state-dir', directory],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(200):
            if server.poll() is not None: raise SystemExit('Local TLS server failed to start')
            try:
                # Only readiness probe bypasses trust. Actual clients verify the exact pin.
                urllib.request.urlopen(f'https://127.0.0.1:{port}/health', context=ssl._create_unverified_context(), timeout=.2).close()
                break
            except OSError:
                time.sleep(.02)
        cert = Path(directory, 'certificate.pem')
        pin = hashlib.sha256(ssl.PEM_cert_to_DER_cert(cert.read_text())).hexdigest()
        for arguments, fingerprint in [(['--four-viewers'], pin), (['--probe-tls-refusal'], '0' * 64)]:
            result = subprocess.run(['build/session-test', *arguments], env=env | {'LAZARUS_TLS_PIN': fingerprint}, timeout=35)
            if result.returncode: raise SystemExit(result.returncode)
    finally:
        server.terminate(); server.wait(timeout=3)
print('Local TLS, exact certificate pin and rejection of a substituted certificate passed')
