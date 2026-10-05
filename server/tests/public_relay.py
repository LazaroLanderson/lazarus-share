"""Public CA/WSS, approved temporary participants and actual encrypted TURN media.
Never prints room/admin secrets, credentials, IPs, SDP or raw subprocess output.
"""
import asyncio
import hashlib
import os
import secrets
import sys
from urllib.parse import quote, urlsplit, urlunsplit
import aiohttp

async def receive(ws, expected):
    async def read():
        while True:
            message=await ws.receive_json()
            if message.get('type') == expected: return message
            if message.get('type') != 'room-state': raise RuntimeError('Unexpected signaling stage')
    return await asyncio.wait_for(read(),10)

async def main(hostname, binary):
    room=hashlib.sha256(secrets.token_bytes(32)).hexdigest()
    async with aiohttp.ClientSession() as client:
        async with client.get('https://'+hostname+'/health') as health:
            if health.status != 200: raise RuntimeError('Public health failed')
        async with client.ws_connect('wss://'+hostname+'/ws') as host, client.ws_connect('wss://'+hostname+'/ws') as viewer:
            try:
                await host.send_json({'type':'create','protocol':2,'room':room,'admin':secrets.token_hex(32),'challenge':secrets.token_hex(16)})
                created=await receive(host,'created')
                await viewer.send_json({'type':'join','protocol':2,'room':room,'challenge':secrets.token_hex(16)})
                joined=await receive(viewer,'joined');await receive(host,'waiting')
                await host.send_json({'type':'approve','peer':joined['peer']})
                await receive(host,'room-state');await receive(host,'room-state')
                await host.send_json({'type':'share-request'})
                reservation=await receive(host,'room-state')
                await host.send_json({'type':'share-confirm','revision':reservation['revision']})
                await receive(host,'ready');await receive(viewer,'ready')
                await host.send_json({'type':'relay','peer':joined['peer'],'revision':reservation['revision'],'enabled':True})
                await viewer.send_json({'type':'relay','peer':created['peer'],'revision':reservation['revision'],'enabled':True})
                config=await receive(host,'turn');await receive(viewer,'turn')
                endpoints=config.get('endpoints',[])
                if len(endpoints)!=3: raise RuntimeError('TURN endpoints incomplete')
                for index,endpoint in enumerate(endpoints):
                    parsed=urlsplit(endpoint)
                    if parsed.hostname != hostname or parsed.scheme != ('turn','turn','turns')[index] or parsed.port != (3478,3478,5349)[index] or parsed.query != ('','transport=tcp','')[index]:raise RuntimeError('Unexpected TURN endpoint')
                    authority=quote(config['username'],safe='')+':'+quote(config['password'],safe='')+'@'+parsed.netloc
                    uri=urlunsplit((parsed.scheme,authority,'',parsed.query,''))
                    process=await asyncio.create_subprocess_exec(binary,'--turn='+uri,stdout=asyncio.subprocess.DEVNULL,stderr=asyncio.subprocess.DEVNULL)
                    try: code=await asyncio.wait_for(process.wait(),30)
                    except asyncio.TimeoutError:
                        process.kill();await process.wait();raise RuntimeError('TURN media test timed out')
                    if code:raise RuntimeError('TURN media transport '+('UDP','TCP','TLS')[index]+' failed')
                    print('Public encrypted TURN media passed:',('UDP','TCP','TLS')[index],flush=True)
            finally:
                if not host.closed: await host.send_json({'type':'end'})
    print('Public TLS, WSS approval and TURN media verified; temporary room closed')

if __name__=='__main__':
    if len(sys.argv)!=3:raise SystemExit('Usage: public_relay.py HOSTNAME MEDIA_TEST_BINARY')
    try:asyncio.run(main(sys.argv[1],sys.argv[2]))
    except Exception as error:
        # Network exception bodies can contain addresses: only print our fixed stage errors.
        print(str(error) if isinstance(error,RuntimeError) else 'Public verification failed: '+type(error).__name__,file=sys.stderr)
        raise SystemExit(1)
