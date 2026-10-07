"""Ephemeral room coordination. Never receives invitation secrets or media."""
import asyncio
import base64
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import secrets
import time
import unicodedata
import regex
from dataclasses import dataclass, field
from aiohttp import web, WSMsgType

HEX = re.compile(r"^[0-9a-f]{64}$")
CHALLENGE = re.compile(r"^[0-9a-f]{32}$")


@dataclass
class Participant:
    ws: web.WebSocketResponse | None
    challenge: str
    approved: bool = True
    profile: dict = field(default_factory=dict)
    capabilities: list = field(default_factory=list)


@dataclass
class Pair:
    session: str = field(default_factory=lambda: secrets.token_hex(16))
    consent: dict = field(default_factory=dict)
    issued_at: float = -1000


@dataclass
class Room:
    require_approval: bool
    admin_hash: bytes
    owner: str
    participants: dict = field(default_factory=dict)
    expiry: float | None = None
    broadcaster: str = ""
    revision: int = 0
    sharing: bool = False
    reservation_expiry: float | None = None
    pairs: dict = field(default_factory=dict)
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)


def participant_profile(message):
    value = message.get('profile', {})
    if not isinstance(value, dict): raise ValueError()
    name = value.get('nickname', '')
    avatar = value.get('avatar', 0)
    if not isinstance(name, str) or not isinstance(avatar, int) or isinstance(avatar, bool) or not 0 <= avatar < 10: raise ValueError()
    name = unicodedata.normalize('NFC', name).strip()
    if name and (len(name) > 80 or len(regex.findall(r'\X', name)) > 12 or any(unicodedata.category(c) in ('Cc','Cf','Zl','Zp') for c in name)): raise ValueError()
    return {'nickname': name, 'avatar': avatar}


def capabilities(message):
    value = message.get('capabilities', [])
    return [item for item in ('profile', 'sharing', 'turn-endpoints') if isinstance(value, list) and item in value]


class Service:
    def __init__(self, *, grace=60, clock=time.monotonic, turn_secret="", turn_host="", turn_endpoints=None):
        self.rooms = {}
        self.grace, self.clock = grace, clock
        self.turn_secret, self.turn_host = turn_secret, turn_host
        self.rates = {}
        self.closing = set()
        self.turn_endpoints = turn_endpoints or ([f"turn://{turn_host}:3478", f"turn://{turn_host}:3478?transport=tcp", f"turns://{turn_host}:5349"] if turn_host else [])

    async def send(self, ws, **message):
        if ws is not None and not ws.closed:
            try:
                await ws.send_json(message)
            except (ConnectionError, RuntimeError):
                pass

    def close_socket(self, ws):
        # Do not hold the room lock while waiting for a remote close handshake.
        if ws is None: return
        task = asyncio.create_task(ws.close())
        self.closing.add(task)
        task.add_done_callback(self.closing.discard)

    async def close_room(self, key):
        room = self.rooms.pop(key, None)
        if room:
            for participant in list(room.participants.values()):
                await self.send(participant.ws, type="ended")
                self.close_socket(participant.ws)

    async def state(self, room):
        for pid, participant in room.participants.items():
            if not participant.approved: continue
            roster = [dict(peer=i, profile=p.profile, approved=p.approved, owner=i == room.owner)
                      for i, p in room.participants.items() if p.approved or pid == room.owner]
            await self.send(participant.ws, type="room-state", protocol=2, participants=roster,
                            broadcaster=room.broadcaster, revision=room.revision, sharing=room.sharing,
                            ownerOnline=room.participants[room.owner].ws is not None)

    async def release(self, room):
        if not room.broadcaster: return
        room.broadcaster = ""
        room.sharing = False
        room.reservation_expiry = None
        room.revision += 1
        room.pairs.clear()
        await self.state(room)

    async def reap(self):
        now = self.clock()
        for key, room in list(self.rooms.items()):
            async with room.lock:
                if room.expiry is not None and room.expiry <= now:
                    await self.close_room(key)
                elif room.reservation_expiry is not None and room.reservation_expiry <= now:
                    await self.release(room)
        self.rates = {k: v for k, v in self.rates.items() if now - v[0] < 60}

    def limited(self, remote):
        now = self.clock()
        start, count = self.rates.get(remote, (now, 0))
        if now - start >= 60: start, count = now, 0
        self.rates[remote] = (start, count + 1)
        return count >= 30

    async def ready(self, room, receiver):
        sender = room.broadcaster
        if not room.sharing or receiver == sender: return
        pair = Pair()
        room.pairs[receiver] = pair
        for local, remote in ((sender, receiver), (receiver, sender)):
            p = room.participants[remote]
            await self.send(room.participants[local].ws, type="ready", protocol=2, peer=remote,
                            session=pair.session, challenge=p.challenge, profile=p.profile,
                            capabilities=p.capabilities, revision=room.revision)

    def pair(self, room, local, remote, revision):
        if type(revision) is not int or not room.sharing or revision != room.revision or local == remote: raise ValueError()
        if local == room.broadcaster: receiver = remote
        elif remote == room.broadcaster: receiver = local
        else: raise ValueError()
        pair = room.pairs.get(receiver)
        if not pair: raise ValueError()
        return pair

    async def turn(self, room, local, remote, pair):
        if not (pair.consent.get(local) and pair.consent.get(remote)) or self.clock() - pair.issued_at < 30: return
        if not self.turn_secret or not self.turn_host:
            for a, b in ((local, remote), (remote, local)):
                await self.send(room.participants[a].ws, type="error", code="turn_unavailable", peer=b, revision=room.revision)
            return
        pair.issued_at = self.clock()
        username = f"{int(time.time()) + 3600}:{secrets.token_hex(8)}"
        password = base64.b64encode(hmac.new(self.turn_secret.encode(), username.encode(), hashlib.sha1).digest()).decode()
        for a, b in ((local, remote), (remote, local)):
            await self.send(room.participants[a].ws, type="turn", peer=b, revision=room.revision,
                            username=username, password=password, host=self.turn_host, expires=3600, endpoints=self.turn_endpoints)

    async def websocket(self, request):
        if self.limited(request.remote): return web.Response(status=429)
        ws = web.WebSocketResponse(heartbeat=20, max_msg_size=131072, compress=False)
        await ws.prepare(request)
        key = pid = None
        messages, window = 0, self.clock()
        try:
            async for msg in ws:
                if msg.type != WSMsgType.TEXT: continue
                if self.clock() - window > 1: window, messages = self.clock(), 0
                messages += 1
                room = self.rooms.get(key)
                budget = 512 if room and pid == room.broadcaster else 100
                if messages > budget:
                    await ws.close(code=1008); break
                try:
                    m = json.loads(msg.data)
                    if not isinstance(m, dict): raise ValueError()
                    kind = m.get("type")
                    await self.reap()
                    if pid is None:
                        if type(m.get("protocol")) is not int or m["protocol"] != 2:
                            await self.send(ws, type="error", code="protocol_update_required", protocol=2); continue
                        candidate, challenge = m.get("room", ""), m.get("challenge", "")
                        if not isinstance(candidate, str) or not HEX.fullmatch(candidate) or not isinstance(challenge, str) or not CHALLENGE.fullmatch(challenge): raise ValueError()
                        if self.limited(request.remote):
                            await self.send(ws, type="error", code="rate_limit"); continue
                        profile, caps = participant_profile(m), capabilities(m)
                        if kind == "create":
                            admin = m.get("admin", "")
                            if not isinstance(admin, str) or not HEX.fullmatch(admin) or candidate in self.rooms or len(self.rooms) >= 1000:
                                await self.send(ws, type="error", code="create_failed"); continue
                            policy = m.get("requireApproval", True)
                            if not isinstance(policy, bool): raise ValueError()
                            key, pid = candidate, secrets.token_hex(8)
                            room = Room(policy, hashlib.sha256(admin.encode()).digest(), pid)
                            room.participants[pid] = Participant(ws, challenge, profile=profile, capabilities=caps)
                            self.rooms[key] = room
                            async with room.lock:
                                await self.send(ws, type="created", protocol=2, peer=pid, requireApproval=policy)
                                await self.state(room)
                        elif kind in ("join", "resume"):
                            room = self.rooms.get(candidate)
                            if room is None:
                                await self.send(ws, type="error", code="resume_failed" if kind == "resume" else "room_unavailable"); continue
                            async with room.lock:
                                owner = room.participants[room.owner]
                                if kind == "resume":
                                    admin = m.get("admin", "")
                                    if not isinstance(admin, str) or owner.ws is not None or not hmac.compare_digest(room.admin_hash, hashlib.sha256(admin.encode()).digest()):
                                        await self.send(ws, type="error", code="resume_failed"); continue
                                    key, pid = candidate, room.owner
                                    room.participants[pid] = Participant(ws, challenge, profile=profile, capabilities=caps)
                                    room.expiry = None
                                    await self.send(ws, type="created", protocol=2, peer=pid, requireApproval=room.require_approval)
                                    for i, p in room.participants.items():
                                        if not p.approved: await self.send(ws, type="waiting", peer=i, profile=p.profile, capabilities=p.capabilities)
                                    await self.state(room)
                                    if room.sharing: await self.ready(room, pid)
                                else:
                                    if owner.ws is None or len(room.participants) >= 17:
                                        await self.send(ws, type="error", code="room_unavailable"); continue
                                    if not room.require_approval and sum(p.approved for p in room.participants.values()) >= 5:
                                        await self.send(ws, type="error", code="room_full"); continue
                                    key, pid = candidate, secrets.token_hex(8)
                                    approved = not room.require_approval
                                    room.participants[pid] = Participant(ws, challenge, approved, profile, caps)
                                    await self.send(ws, type="joined", protocol=2, peer=pid, requireApproval=room.require_approval)
                                    if not approved: await self.send(owner.ws, type="waiting", peer=pid, profile=profile, capabilities=caps)
                                    await self.state(room)
                                    if approved and room.sharing: await self.ready(room, pid)
                        else: raise ValueError()
                        continue
                    room = self.rooms.get(key)
                    if not room: break
                    async with room.lock:
                        p = room.participants.get(pid)
                        if not p or p.ws is not ws: break
                        if kind == "end" and pid == room.owner:
                            await self.close_room(key); break
                        if kind in ("approve", "remove") and pid == room.owner:
                            target = m.get("peer")
                            guest = room.participants.get(target) if isinstance(target, str) else None
                            if not guest or target == room.owner:
                                await self.send(ws, type="error", code="unknown_peer"); continue
                            if kind == "remove":
                                room.participants.pop(target)
                                if room.broadcaster == target: await self.release(room)
                                room.pairs.pop(target, None)
                                await self.send(guest.ws, type="ended")
                                self.close_socket(guest.ws)
                                await self.state(room)
                            elif not guest.approved:
                                if sum(p.approved for p in room.participants.values()) >= 5:
                                    await self.send(ws, type="error", code="room_full"); continue
                                guest.approved = True
                                await self.state(room)
                                if room.sharing: await self.ready(room, target)
                        elif not p.approved: raise ValueError()
                        elif kind == "share-request":
                            if room.broadcaster:
                                await self.send(ws, type="error", code="share_busy"); continue
                            room.broadcaster = pid
                            room.revision += 1
                            room.reservation_expiry = self.clock() + 60
                            await self.state(room)
                        elif kind in ("share-confirm", "share-release"):
                            if pid != room.broadcaster or type(m.get("revision")) is not int or m["revision"] != room.revision: raise ValueError()
                            if kind == "share-release": await self.release(room)
                            elif not room.sharing:
                                room.sharing, room.reservation_expiry = True, None
                                await self.state(room)
                                for receiver, participant in room.participants.items():
                                    if participant.approved and participant.ws is not None and receiver != pid: await self.ready(room, receiver)
                        elif kind == "profile":
                            p.profile = participant_profile(m)
                            await self.state(room)
                        elif kind in ("signal", "relay"):
                            remote = m.get("peer")
                            if not isinstance(remote, str): raise ValueError()
                            pair = self.pair(room, pid, remote, m.get("revision"))
                            if kind == "signal":
                                payload, mac = m.get("payload"), m.get("mac")
                                if not isinstance(payload, str) or len(payload) > 120000 or not isinstance(mac, str) or not HEX.fullmatch(mac): raise ValueError()
                                await self.send(room.participants[remote].ws, type="signal", peer=pid, revision=room.revision, payload=payload, mac=mac)
                            else:
                                if not isinstance(m.get("enabled"), bool): raise ValueError()
                                pair.consent[pid] = m["enabled"]
                                await self.turn(room, pid, remote, pair)
                        else: raise ValueError()
                except (ValueError, TypeError, KeyError):
                    await self.send(ws, type="error", code="invalid_message")
        finally:
            room = self.rooms.get(key)
            if room:
                async with room.lock:
                    p = room.participants.get(pid)
                    if p and p.ws is ws:
                        if pid == room.owner:
                            p.ws = None
                            room.expiry = self.clock() + self.grace
                        else: room.participants.pop(pid)
                        if room.broadcaster == pid: await self.release(room)
                        room.pairs.pop(pid, None)
                        if pid == room.owner:
                            for participant in room.participants.values(): await self.send(participant.ws, type="host_offline")
                        await self.state(room)
        return ws


def application(service=None):
    service = service or Service(turn_secret=os.getenv("TURN_SECRET", ""), turn_host=os.getenv("TURN_HOST", ""), turn_endpoints=json.loads(os.getenv("TURN_ENDPOINTS", "null")))
    app = web.Application(client_max_size=131072)
    app.router.add_get("/ws", service.websocket)
    async def health(_):
        return web.json_response({"ok": True})
    app.router.add_get("/health", health)
    async def invitation(_):
        return web.Response(text=Path(__file__).with_name("join.html").read_text(), content_type="text/html",
                            headers={"Cache-Control": "no-store", "Referrer-Policy": "no-referrer",
                                     "X-Content-Type-Options": "nosniff"})
    app.router.add_get("/join", invitation)

    async def sweep(_):
        async def loop():
            while True:
                await asyncio.sleep(1)
                await service.reap()
        task = asyncio.create_task(loop())
        yield
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass
        for key in list(service.rooms):
            await service.close_room(key)
        if service.closing:
            await asyncio.gather(*service.closing, return_exceptions=True)
    app.cleanup_ctx.append(sweep)
    return app


if __name__ == "__main__":
    # TLS terminated by the supplied proxy; bind locally outside containers.
    web.run_app(application(), host=os.getenv("BIND", "127.0.0.1"), port=int(os.getenv("PORT", "8080")),
                access_log=None, print=None)
