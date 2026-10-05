"""Protocol v2 admission, exclusive broadcasting and per-pair relay tests."""
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
        async def read():
            while True:
                m = await ws.receive_json()
                if m['type'] == kind: return m
                self.assertEqual(m['type'], 'room-state', m)
        return await asyncio.wait_for(read(), 2)

    async def state(self, ws, **expected):
        while True:
            m = await self.receive(ws, 'room-state')
            if all(m.get(k) == v for k, v in expected.items()): return m

    async def host(self, approval=True):
        ws = await self.socket()
        await ws.send_json(dict(type="create", protocol=2, room=self.room, admin=self.admin,
                                challenge="a" * 32, requireApproval=approval,
                                profile=dict(nickname="Criador", avatar=3)))
        created = await self.receive(ws, "created")
        await self.state(ws)
        return ws, created['peer']

    async def guest(self, host, approved=True, policy=True):
        ws = await self.socket()
        await ws.send_json(dict(type="join", protocol=2, room=self.room, challenge="b" * 32,
                                profile=dict(nickname="Convidado", avatar=2)))
        joined = await self.receive(ws, "joined")
        if policy:
            await self.receive(host, "waiting")
            await self.state(host)
            if approved: await host.send_json(dict(type="approve", peer=joined["peer"]))
        if approved:
            await self.state(host)
            await self.state(ws)
        return ws, joined['peer']

    async def reserve(self, ws, pid):
        await ws.send_json(dict(type='share-request'))
        return (await self.state(ws, broadcaster=pid))['revision']

    async def start(self, ws, pid, receivers):
        revision = await self.reserve(ws, pid)
        await ws.send_json(dict(type='share-confirm', revision=revision))
        await self.state(ws, sharing=True)
        pairs = {}
        for receiver, rid in receivers:
            h = await self.receive(ws, 'ready')
            g = await self.receive(receiver, 'ready')
            self.assertEqual(h['peer'], rid)
            self.assertEqual(g['peer'], pid)
            self.assertEqual(h['session'], g['session'])
            self.assertEqual(h['revision'], revision)
            pairs[rid] = h
        return revision, pairs

    async def test_protocol_version_required(self):
        ws = await self.socket()
        for version in (None, 1, True, 3):
            await ws.send_json(dict(type='create', room=self.room, admin=self.admin, challenge='a'*32, protocol=version))
            self.assertEqual((await self.receive(ws, 'error'))['code'], 'protocol_update_required')
        self.assertFalse(self.service.rooms)

    async def test_admission_limit_pending_and_admin_permissions(self):
        host, hid = await self.host()
        guests = [await self.guest(host) for _ in range(4)]
        fifth, pid = await self.guest(host, False)
        await host.send_json(dict(type='approve', peer=pid))
        self.assertEqual((await self.receive(host, 'error'))['code'], 'room_full')
        for kind in ('share-request', 'signal', 'relay', 'end'):
            await fifth.send_json(dict(type=kind))
            self.assertEqual((await self.receive(fifth, 'error'))['code'], 'invalid_message')
        await guests[0][0].send_json(dict(type='remove', peer=hid))
        self.assertEqual((await self.receive(guests[0][0], 'error'))['code'], 'invalid_message')
        await host.send_json(dict(type='remove', peer=guests[0][1]))
        await self.receive(guests[0][0], 'ended')
        await host.send_json(dict(type='approve', peer=pid))
        m = await self.state(fifth)
        self.assertEqual(sum(p['approved'] for p in m['participants']), 5)

    async def test_concurrent_auto_admission(self):
        host, _ = await self.host(False)
        guests = [await self.socket() for _ in range(8)]
        original = self.service.send
        async def yielding(ws, **m):
            await asyncio.sleep(.002)
            await original(ws, **m)
        self.service.send = yielding
        await asyncio.gather(*(ws.send_json(dict(type='join', protocol=2, room=self.room, challenge='b'*32)) for ws in guests))
        results = await asyncio.gather(*(asyncio.wait_for(ws.receive_json(), 2) for ws in guests))
        self.assertEqual(sum(m['type'] == 'joined' for m in results), 4)
        self.assertEqual(sum(m.get('code') == 'room_full' for m in results), 4)
        self.assertEqual(len(self.service.rooms[self.room].participants), 5)

    async def test_exclusive_slot_cancel_timeout_and_stale_confirm(self):
        host, hid = await self.host()
        guest, gid = await self.guest(host)
        await asyncio.gather(host.send_json(dict(type='share-request')), guest.send_json(dict(type='share-request')))
        room = self.service.rooms[self.room]
        # Allow both socket loops to process their requests.
        await asyncio.sleep(.05)
        self.assertIn(room.broadcaster, (hid, gid))
        winner, loser = (host, guest) if room.broadcaster == hid else (guest, host)
        revision = room.revision
        self.assertEqual((await self.receive(loser, 'error'))['code'], 'share_busy')
        await winner.send_json(dict(type='share-release', revision=revision))
        await self.state(winner, broadcaster='')
        self.assertGreater(room.revision, revision)
        await winner.send_json(dict(type='share-confirm', revision=revision))
        await self.receive(winner, 'error')
        revision = await self.reserve(host, hid)
        self.now = 60
        await self.service.reap()
        await self.state(host, broadcaster='')
        self.assertFalse(room.pairs)
        await host.send_json(dict(type='share-confirm', revision=revision))
        await self.receive(host, 'error')

    async def test_guest_transmits_to_owner_and_other_guests(self):
        host, hid = await self.host()
        first, fid = await self.guest(host)
        second, sid = await self.guest(host)
        revision, pairs = await self.start(first, fid, [(host, hid), (second, sid)])
        self.assertEqual(set(pairs), {hid, sid})
        await first.send_json(dict(type='signal', peer=sid, revision=revision, payload='opaque', mac='f'*64))
        forwarded = await self.receive(second, 'signal')
        self.assertEqual(forwarded['peer'], fid)
        self.assertEqual(forwarded['payload'], 'opaque')
        await second.send_json(dict(type='signal', peer=hid, revision=revision, payload='opaque', mac='f'*64))
        await self.receive(second, 'error')
        await first.send_json(dict(type='share-release', revision=revision))
        await self.state(first, broadcaster='')
        new_revision, _ = await self.start(second, sid, [(host, hid), (first, fid)])
        self.assertGreater(new_revision, revision)
        await first.send_json(dict(type='signal', peer=sid, revision=revision, payload='old', mac='f'*64))
        await self.receive(first, 'error')

    async def test_join_while_sharing_and_remove_broadcaster(self):
        host, hid = await self.host(False)
        await self.start(host, hid, [])
        guest, gid = await self.guest(host, policy=False)
        h = await self.receive(host, 'ready'); g = await self.receive(guest, 'ready')
        self.assertEqual(h['session'], g['session'])
        await host.send_json(dict(type='share-release', revision=h['revision']))
        await self.state(host, broadcaster='')
        await self.start(guest, gid, [(host, hid)])
        await host.send_json(dict(type='remove', peer=gid))
        await self.receive(guest, 'ended')
        await self.state(host, broadcaster='')
        self.assertEqual(len(self.service.rooms[self.room].participants), 1)

    async def test_relay_consent_per_pair_renewal_and_stale_reply(self):
        host, hid = await self.host()
        sender, sid = await self.guest(host)
        other, oid = await self.guest(host)
        revision, _ = await self.start(sender, sid, [(host, hid), (other, oid)])
        await sender.send_json(dict(type='relay', peer=hid, revision=revision, enabled=True))
        await host.send_json(dict(type='relay', peer=sid, revision=revision, enabled=True))
        first = await self.receive(host, 'turn'); peer = await self.receive(sender, 'turn')
        self.assertEqual(first['username'], peer['username'])
        self.assertEqual(first['peer'], sid)
        self.assertEqual(len(first['endpoints']), 3)
        self.assertEqual(self.service.rooms[self.room].pairs[oid].issued_at, -1000)
        self.now = 31
        await host.send_json(dict(type='relay', peer=sid, revision=revision, enabled=False))
        await sender.send_json(dict(type='relay', peer=hid, revision=revision, enabled=True))
        await asyncio.sleep(.05)
        self.assertEqual(self.service.rooms[self.room].pairs[hid].issued_at, 0)
        await host.send_json(dict(type='relay', peer=sid, revision=revision, enabled=True))
        renewed = await self.receive(host, 'turn'); await self.receive(sender, 'turn')
        self.assertNotEqual(first['username'], renewed['username'])
        await sender.send_json(dict(type='share-release', revision=revision))
        await self.state(sender, broadcaster='')
        await host.send_json(dict(type='relay', peer=sid, revision=revision, enabled=True))
        await self.receive(host, 'error')

    async def test_broadcaster_disconnect_releases_slot(self):
        host, hid = await self.host()
        guest, gid = await self.guest(host)
        await self.start(guest, gid, [(host, hid)])
        await guest.close()
        await self.state(host, broadcaster='')
        self.assertFalse(self.service.rooms[self.room].pairs)
        await self.start(host, hid, [])

    async def test_owner_resume_with_guest_broadcast_and_expiry(self):
        host, hid = await self.host()
        guest, gid = await self.guest(host)
        _, pairs = await self.start(guest, gid, [(host, hid)])
        await host.close(); await self.receive(guest, 'host_offline')
        self.now = 59; await self.service.reap()
        attacker = await self.socket()
        await attacker.send_json(dict(type='resume', protocol=2, room=self.room, admin='b'*64, challenge='c'*32))
        self.assertEqual((await self.receive(attacker, 'error'))['code'], 'resume_failed')
        resumed = await self.socket()
        await resumed.send_json(dict(type='resume', protocol=2, room=self.room, admin=self.admin, challenge='d'*32))
        self.assertEqual((await self.receive(resumed, 'created'))['peer'], hid)
        ready = await self.receive(resumed, 'ready'); await self.receive(guest, 'ready')
        self.assertNotEqual(ready['session'], pairs[hid]['session'])
        await resumed.close(); await self.receive(guest, 'host_offline')
        self.now = 120; await self.service.reap()
        await self.receive(guest, 'ended'); self.assertFalse(self.service.rooms)

    async def test_owner_broadcast_does_not_resume_capture(self):
        host, hid = await self.host()
        guest, gid = await self.guest(host)
        await self.start(host, hid, [(guest, gid)])
        await host.close(); await self.receive(guest, 'host_offline')
        self.assertFalse(self.service.rooms[self.room].broadcaster)

    async def test_end_and_profile_validation(self):
        host, hid = await self.host()
        guest, gid = await self.guest(host)
        await guest.send_json(dict(type='profile', profile=dict(nickname='abcdefghijk', avatar=0)))
        await self.receive(guest, 'error')
        await guest.send_json(dict(type='profile', profile=dict(nickname='Jose\u0301', avatar=9)))
        m = await self.state(guest)
        self.assertEqual(next(p['profile'] for p in m['participants'] if p['peer']==gid), dict(nickname='José', avatar=9))
        await host.send_json(dict(type='end'))
        await self.receive(host, 'ended'); await self.receive(guest, 'ended')
        self.assertFalse(self.service.rooms)

    async def test_invalid_policy_unknown_room_and_input(self):
        ws = await self.socket()
        for value in ('false', 0, None):
            await ws.send_json(dict(type='create', protocol=2, room=self.room, admin=self.admin, challenge='a'*32, requireApproval=value))
            self.assertEqual((await self.receive(ws, 'error'))['code'], 'invalid_message')
        await ws.send_json(dict(type='join', protocol=2, room=[], challenge='b'*32))
        await self.receive(ws, 'error')
        await ws.send_json(dict(type='join', protocol=2, room=self.room, challenge='b'*32))
        self.assertEqual((await self.receive(ws, 'error'))['code'], 'room_unavailable')

    async def test_invitation_page_and_rate_limit(self):
        response = await self.client.get('/join')
        self.assertEqual(response.status, 200)
        self.assertEqual(response.headers['Referrer-Policy'], 'no-referrer')
        text = await response.text()
        for fragment in ('location.hash', 'lazarus-share://join#', 'navigator.clipboard'): self.assertIn(fragment, text)
        for _ in range(30): self.assertFalse(self.service.limited('test-ip'))
        self.assertTrue(self.service.limited('test-ip'))
        self.now = 61; await self.service.reap()
        self.assertNotIn('test-ip', self.service.rates)

    async def test_broadcaster_ice_budget_is_bounded(self):
        host, hid = await self.host()
        guest, gid = await self.guest(host)
        revision, _ = await self.start(guest, gid, [(host, hid)])
        for _ in range(150):
            await guest.send_json(dict(type='signal', peer=hid, revision=revision, payload='candidate', mac='f'*64))
            await self.receive(host, 'signal')
        room = self.service.rooms[self.room]
        self.assertEqual(room.broadcaster, gid)
        for _ in range(400): await guest.send_json(dict(type='invalid'))
        while True:
            m = await asyncio.wait_for(guest.receive(), 2)
            if m.type == WSMsgType.CLOSE:
                self.assertEqual(m.data, 1008); break


if __name__ == '__main__': unittest.main()
