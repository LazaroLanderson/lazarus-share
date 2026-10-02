"""Exercise real Qt TLS backend: exact pin, substituted pin and IP SAN mismatch."""
import base64
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading


class Upgrade(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *args):
        pass

    def do_GET(self):
        key = self.headers['Sec-WebSocket-Key']
        accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
        self.send_response(101)
        self.send_header('Upgrade', 'websocket')
        self.send_header('Connection', 'Upgrade')
        self.send_header('Sec-WebSocket-Accept', accept)
        self.end_headers()
        self.wfile.flush()
        self.close_connection = True


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    binary = Path('build/tls-test.exe' if Path('build/tls-test.exe').exists() else 'build/tls-test')
    for wrong_host in (False, True):
        cert, key = root / 'cert.pem', root / 'key.pem'
        name = 'wrong.example.invalid' if wrong_host else '127.0.0.1'
        san = 'DNS:' + name if wrong_host else 'IP:' + name
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=' + name, '-addext', 'subjectAltName=' + san, '-keyout', str(key), '-out', str(cert)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        fingerprint = hashlib.sha256(ssl.PEM_cert_to_DER_cert(cert.read_text())).hexdigest()
        server = ThreadingHTTPServer(('127.0.0.1', 0), Upgrade)
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(cert, key)
        server.socket = tls.wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            endpoint = 'wss://127.0.0.1:' + str(server.server_port) + '/ws'
            if wrong_host:
                subprocess.run([str(binary), endpoint, fingerprint, '--refuse'], check=True, timeout=10)
            else:
                subprocess.run([str(binary), '--policy', str(cert)], check=True, timeout=10)
                subprocess.run([str(binary), endpoint, fingerprint], check=True, timeout=10)
                subprocess.run([str(binary), endpoint, '0' * 64, '--refuse'], check=True, timeout=10)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=3)
print('Native Qt pinned IP TLS and rejection tests passed')
