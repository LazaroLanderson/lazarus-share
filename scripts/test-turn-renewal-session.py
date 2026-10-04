"""Exercise credential renewal with four viewers against isolated signaling/TURN."""
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1',0)); return s.getsockname()[1]
turnserver=os.getenv('LAZARUS_TURNSERVER') or shutil.which('turnserver')
if not turnserver: raise SystemExit('Install Coturn or set LAZARUS_TURNSERVER')
with tempfile.TemporaryDirectory(prefix='lazarus-auto-relay-') as directory:
    udp,tls,signal=port(),port(),port()
    cert,key=Path(directory,'cert.pem'),Path(directory,'key.pem')
    subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-subj','/CN=localhost','-keyout',str(key),'-out',str(cert)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    secret='integration-test-only-secret'
    turn=subprocess.Popen([turnserver,'-n','--no-cli','--no-dtls','--allow-loopback-peers','--listening-ip=127.0.0.1','--relay-ip=127.0.0.1',f'--listening-port={udp}',f'--tls-listening-port={tls}','--cert='+str(cert),'--pkey='+str(key),'--min-port=55000','--max-port=55040','--user-quota=8','--total-quota=64','--use-auth-secret','--static-auth-secret='+secret,'--realm=lazarus.test','--log-file=/dev/null','--no-stdout-log','--pidfile='+directory+'/turn.pid'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    endpoints=[f'turn://127.0.0.1:{udp}',f'turn://127.0.0.1:{udp}?transport=tcp',f'turns://127.0.0.1:{tls}']
    env=os.environ|{'BIND':'127.0.0.1','PORT':str(signal),'TURN_SECRET':secret,'TURN_HOST':'127.0.0.1','TURN_ENDPOINTS':json.dumps(endpoints),'LAZARUS_SIGNAL_URL':f'ws://127.0.0.1:{signal}/ws','LAZARUS_TLS_PIN':'','LAZARUS_STUN_URL':'','QT_QPA_PLATFORM':'offscreen','PIPEWIRE_REMOTE':'lazarus-nonexistent-test','GST_DEBUG':'0'}
    # Accelerated monotonic issuance clock avoids a 30-second rate-limit wait
    # when exercising early renewal; production signaling remains unchanged.
    fixture='from aiohttp import web; from server.service import Service,application; import os,time; web.run_app(application(Service(clock=lambda: time.monotonic()*100, grace=6000, turn_secret=os.environ["TURN_SECRET"], turn_host=os.environ["TURN_HOST"], turn_endpoints=__import__("json").loads(os.environ["TURN_ENDPOINTS"]))),host="127.0.0.1",port=int(os.environ["PORT"]),access_log=None,print=None)'
    service=subprocess.Popen([sys.executable,'-c',fixture],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            try: urllib.request.urlopen(f'http://127.0.0.1:{signal}/health',timeout=.2).close();break
            except OSError:
                if service.poll() is not None: raise SystemExit('Signaling fixture failed')
                time.sleep(.05)
        subprocess.run(['build/session-test','--relay-auto','--relay-transport=0','--four-viewers','--turn-renewal'],env=env,timeout=35,check=True)
    finally:
        service.terminate();turn.terminate();service.wait(timeout=3);turn.wait(timeout=3)
print('Four-viewer renewal, retry isolation, duplicate responses and pause passed')
