"""Exercise encrypted WebRTC through a local TURN server, without screen capture."""
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

turnserver = os.getenv("LAZARUS_TURNSERVER") or shutil.which("turnserver")
if not turnserver:
    raise SystemExit("Set LAZARUS_TURNSERVER or install Coturn on the development machine")
with tempfile.TemporaryDirectory(prefix="lazarus-turn-test-") as tmp:
    # Unprivileged local listener. Never expose allow-loopback-peers publicly.
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0)); port = listener.getsockname()[1]
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0)); tls_port = listener.getsockname()[1]
    cert, key = str(Path(tmp) / "cert.pem"), str(Path(tmp) / "key.pem")
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-subj", "/CN=localhost", "-keyout", key, "-out", cert], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    proc = subprocess.Popen([turnserver, "-n", "--no-cli", "--no-dtls", "--allow-loopback-peers",
        "--listening-ip=127.0.0.1", "--relay-ip=127.0.0.1", f"--listening-port={port}",
        f"--tls-listening-port={tls_port}", "--cert=" + cert, "--pkey=" + key,
        "--min-port=55000", "--max-port=55040", "--lt-cred-mech", "--realm=lazarus.test",
        "--user=test:local-test-password", "--log-file=/dev/null", "--no-stdout-log",
        f"--pidfile={tmp}/turn.pid"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(.5)
        if proc.poll() is not None: raise SystemExit("Local Coturn failed to start")
        uri = f"turn://test:local-test-password@127.0.0.1:{port}"
        result = subprocess.run(["build/media-test", "--turn=" + uri], timeout=25)
        if result.returncode == 0:
            result = subprocess.run(["build/media-test", "--turn=" + uri + "?transport=tcp"], timeout=25)
        if result.returncode == 0:
            uri = f"turns://test:local-test-password@127.0.0.1:{tls_port}"
            result = subprocess.run(["build/media-test", "--turn=" + uri], timeout=25)
    finally:
        proc.terminate()
        try: proc.wait(timeout=3)
        except subprocess.TimeoutExpired: proc.kill(); proc.wait()
    raise SystemExit(result.returncode)
