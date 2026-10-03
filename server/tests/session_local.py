"""Run two complete app windows offscreen against ephemeral localhost signaling."""
import os
import socket
import subprocess
import tempfile
import time
import urllib.request
import sys

with socket.socket() as sock:
    sock.bind(("127.0.0.1", 0)); port = sock.getsockname()[1]
env = os.environ | {"PORT": str(port), "BIND": "127.0.0.1", "LAZARUS_SIGNAL_URL": f"ws://127.0.0.1:{port}/ws",
                    "LAZARUS_TLS_PIN": "", "LAZARUS_STUN_URL": "",
                    "QT_QPA_PLATFORM": "offscreen", "PIPEWIRE_REMOTE": "lazarus-nonexistent-test", "GST_DEBUG": "0"}
server = subprocess.Popen([sys.executable, "server/service.py"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    for _ in range(100):
        try:
            urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=.2).close(); break
        except OSError:
            if server.poll() is not None: raise SystemExit("Local signaling failed to start")
            time.sleep(.02)
    result = subprocess.run(["build/session-test", *sys.argv[1:]], env=env, timeout=25)
finally:
    server.terminate(); server.wait(timeout=3)
raise SystemExit(result.returncode)
