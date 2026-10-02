"""Check HTTPS and WebSocket upgrade through the actual Compose proxy in CI."""
import asyncio
import ssl
from pathlib import Path
from aiohttp import ClientSession


async def main():
    tls = ssl.create_default_context(cafile=str(Path('infra/certs/signal/fullchain.pem')))
    async with ClientSession() as client:
        async with client.get('https://localhost/health', ssl=tls) as response:
            assert response.status == 200
            assert await response.json() == {'ok': True}
        async with client.get('https://127.0.0.1/health', ssl=tls) as response:
            assert response.status == 200
            assert await response.json() == {'ok': True}
        async with client.ws_connect('wss://127.0.0.1/ws', ssl=tls) as ws:
            await ws.send_json({'type': 'create', 'room': 'a' * 64, 'admin': 'b' * 64, 'challenge': 'c' * 32})
            result = await asyncio.wait_for(ws.receive_json(), 5)
            assert result['type'] == 'created'
    print('Compose HTTPS health and WebSocket room creation passed')


asyncio.run(main())
