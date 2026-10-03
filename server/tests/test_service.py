import asyncio
import hashlib
import unittest
from aiohttp import WSMsgType
from aiohttp.test_utils import TestServer, TestClient
from server.service import Service, application


class Rooms(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.now = 0
        self.service = Service(clock=lambda: self.now, turn_secret="test-only", turn_host="turn.example.org")
        self.client = TestClient(TestServer(application(self.service)))
        await self.client.start_server()
        self.sockets = []
        self.room = hashlib.sha256(b"test room").hexdigest()
        self.admin = "a" * 64

    async def asyncTearDown(self):
        for ws in self.sockets:
            await ws.close()
        await self.client.close()

    async def socket(self):
        ws = await self.client.ws_connect("/ws")
        self.sockets.append(ws)
        return ws

    async def receive(self, ws, kind):
        message = await asyncio.wait_for(ws.receive_json(), 2)
        self.assertEqual(message["type"], kind, message)
        return message

    async def host(self):
        ws = await self.socket()
        await ws.send_json(dict(type="create", room=self.room, admin=self.admin, challenge="a" * 32))
        await self.receive(ws, "created")
        return ws

    async def guest(self, host, approved=True):
        ws = await self.socket()
        await ws.send_json(dict(type="join", room=self.room, challenge="b" * 32))
        joined = await self.receive(ws, "joined")
        await self.receive(host, "waiting")
        if approved:
            await host.send_json(dict(type="approve", peer=joined["peer"]))
            h = await self.receive(host, "ready")
            g = await self.receive(ws, "ready")
            self.assertEqual(h["session"], g["session"])
        return ws, joined["peer"]

    async def test_approval_and_four_viewer_limit(self):
        host = await self.host()
        for _ in range(4):
            await self.guest(host)
        fifth, pid = await self.guest(host, False)
        await host.send_json(dict(type="approve", peer=pid))
        self.assertEqual((await self.receive(host, "error"))["code"], "room_full")
        await fifth.send_json(dict(type="signal", payload="e30=", mac="a" * 64))
        self.assertEqual((await self.receive(fifth, "error"))["code"], "invalid_message")

    async def test_no_turn_before_both_consent(self):
        host = await self.host(); guest, pid = await self.guest(host)
        await host.send_json(dict(type="relay", peer=pid, enabled=True))
        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(host.receive_json(), .05)
        await guest.send_json(dict(type="relay", enabled=True))
        h = await self.receive(host, "turn"); g = await self.receive(guest, "turn")
        self.assertEqual(h["username"], g["username"])
        self.assertNotIn("secret", h)
        self.assertEqual(h["host"], "turn.example.org")

    async def test_turn_renewal_and_revocation(self):
        host = await self.host(); guest, pid = await self.guest(host)
        await host.send_json(dict(type="relay",peer=pid,enabled=True))
        await guest.send_json(dict(type="relay",enabled=True))
        first = await self.receive(host,"turn"); await self.receive(guest,"turn")
        self.assertEqual(len(first['endpoints']),3)
        self.assertIn('transport=tcp',first['endpoints'][1])
        self.now=31
        await host.send_json(dict(type="relay",peer=pid,enabled=False))
        await guest.send_json(dict(type="relay",enabled=True))
        with self.assertRaises(asyncio.TimeoutError): await asyncio.wait_for(host.receive_json(),.05)
        await host.send_json(dict(type="relay",peer=pid,enabled=True))
        renewed=await self.receive(host,"turn"); await self.receive(guest,"turn")
        self.assertNotEqual(first['username'],renewed['username'])

    async def test_profiles_are_validated_and_exchanged(self):
        host=await self.socket()
        await host.send_json(dict(type="create",room=self.room,admin=self.admin,challenge="a"*32,profile={'nickname':'Jose\u0301','avatar':9},capabilities=['sharing']))
        await self.receive(host,'created')
        guest=await self.socket()
        await guest.send_json(dict(type='join',room=self.room,challenge='b'*32,profile={'nickname':'abcdefghijk','avatar':0}))
        await self.receive(guest,'error')
        await guest.send_json(dict(type='join',room=self.room,challenge='b'*32,profile={'nickname':'José','avatar':2}))
        joined=await self.receive(guest,'joined'); waiting=await self.receive(host,'waiting')
        self.assertEqual(waiting['profile']['nickname'],'José')
        await host.send_json(dict(type='approve',peer=joined['peer']))
        await self.receive(host,'ready'); ready=await self.receive(guest,'ready')
        self.assertEqual(ready['profile'],{'nickname':'José','avatar':9})
        self.assertIn('sharing',ready['capabilities'])

    async def test_forwarding_is_opaque_and_remove_revokes(self):
        host = await self.host(); guest, pid = await self.guest(host)
        await guest.send_json(dict(type="signal", payload="opaque", mac="f" * 64))
        forwarded = await self.receive(host, "signal")
        self.assertEqual(forwarded["payload"], "opaque")
        await host.send_json(dict(type="remove", peer=pid))
        await self.receive(guest, "ended"); await self.receive(host, "left")
        self.assertNotIn(pid, self.service.rooms[self.room].guests)

    async def test_host_resume_then_expiry(self):
        host = await self.host(); guest, pid = await self.guest(host)
        await host.close(); await self.receive(guest, "host_offline")
        self.now = 59; await self.service.reap(); self.assertIn(self.room, self.service.rooms)
        attacker = await self.socket()
        await attacker.send_json(dict(type="resume", room=self.room, admin="b" * 64, challenge="c" * 32))
        self.assertEqual((await self.receive(attacker, "error"))["code"], "resume_failed")
        resumed = await self.socket()
        await resumed.send_json(dict(type="resume", room=self.room, admin=self.admin, challenge="d" * 32))
        await self.receive(resumed, "created"); await self.receive(resumed, "waiting")
        await self.receive(resumed, "ready"); await self.receive(guest, "ready")
        await resumed.close(); await self.receive(guest, "host_offline")
        self.now = 120; await self.service.reap()
        await self.receive(guest, "ended"); self.assertNotIn(self.room, self.service.rooms)

    async def test_invalid_input_and_unknown_room(self):
        ws = await self.socket()
        await ws.send_json({"type": "join", "room": [], "challenge": "b" * 32})
        await self.receive(ws, "error")
        await ws.send_json({"type": "join", "room": self.room, "challenge": "b" * 32})
        self.assertEqual((await self.receive(ws, "error"))["code"], "room_unavailable")

    async def test_end_erases_room(self):
        host = await self.host(); guest, _ = await self.guest(host)
        await host.send_json(dict(type="end"))
        await self.receive(guest, "ended"); await self.receive(host, "ended")
        self.assertEqual(self.service.rooms, {})

    async def test_host_ice_burst_and_message_limit(self):
        host = await self.host(); guest, pid = await self.guest(host)
        # More than 100 ICE messages can legitimately arrive from four peers.
        for _ in range(150):
            await host.send_json(dict(type="signal", peer=pid, payload="candidate", mac="f" * 64))
            await self.receive(guest, "signal")
        # A larger host budget remains bounded, including invalid requests.
        for _ in range(360):
            await host.send_json(dict(type="signal", peer="unknown"))
            await self.receive(host, "error")
        await host.send_json(dict(type="signal", peer="unknown"))
        closed = await asyncio.wait_for(host.receive(), 2)
        self.assertEqual(closed.type, WSMsgType.CLOSE)
        self.assertEqual(closed.data, 1008)

    async def test_rate_limit_expires(self):
        for _ in range(30): self.assertFalse(self.service.limited("test-ip"))
        self.assertTrue(self.service.limited("test-ip"))
        self.now = 61; await self.service.reap()
        self.assertNotIn("test-ip", self.service.rates)


if __name__ == "__main__":
    unittest.main()
