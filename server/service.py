"""Ephemeral room coordination. Never receives invitation secrets or media."""
import asyncio
import base64
import hashlib
import hmac
import json
import os
import re
import secrets
import time
from dataclasses import dataclass, field
from aiohttp import web, WSMsgType

HEX = re.compile(r"^[0-9a-f]{64}$")
CHALLENGE = re.compile(r"^[0-9a-f]{32}$")


@dataclass
class Guest:
    ws: web.WebSocketResponse
    challenge: str
    approved: bool = False
    session: str = ""
    consent: bool = False
    host_consent: bool = False
    turn_issued: bool = False


@dataclass
class Room:
    host: web.WebSocketResponse | None
    admin_hash: bytes
    challenge: str
    guests: dict = field(default_factory=dict)
    expiry: float | None = None


class Service:
    def __init__(self, *, grace=60, clock=time.monotonic, turn_secret="", turn_host=""):
        self.rooms = {}
        self.grace, self.clock = grace, clock
        self.turn_secret, self.turn_host = turn_secret, turn_host
        self.rates = {}

    async def send(self, ws, **message):
        if ws is not None and not ws.closed:
            try:
                await ws.send_json(message)
            except (ConnectionError, RuntimeError):
                pass

    async def close_room(self, key):
        room = self.rooms.pop(key, None)
        if room:
            for guest in list(room.guests.values()):
                await self.send(guest.ws, type="ended")
                await guest.ws.close()
            await self.send(room.host, type="ended")

    async def reap(self):
        now = self.clock()
        for key, room in list(self.rooms.items()):
            if room.expiry is not None and room.expiry <= now:
                await self.close_room(key)
        self.rates = {k: v for k, v in self.rates.items() if now - v[0] < 60}

    def limited(self, remote):
        # IP keys exist only in RAM, for at most one minute; never logged.
        now = self.clock()
        start, count = self.rates.get(remote, (now, 0))
        if now - start >= 60:
            start, count = now, 0
        self.rates[remote] = (start, count + 1)
        return count >= 30

    async def websocket(self, request):
        if self.limited(request.remote):
            return web.Response(status=429)
        ws = web.WebSocketResponse(heartbeat=20, max_msg_size=131072, compress=False)
        await ws.prepare(request)
        key = role = peer_id = None
        messages = 0
        window = self.clock()
        try:
            async for msg in ws:
                if msg.type != WSMsgType.TEXT:
                    continue
                if self.clock() - window > 1:
                    window, messages = self.clock(), 0
                messages += 1
                # A host gathers ICE for four peers at once; permit its bounded burst.
                if messages > (512 if role == "host" else 100):
                    await ws.close(code=1008)
                    break
                try:
                    m = json.loads(msg.data)
                    if not isinstance(m, dict):
                        raise ValueError()
                    kind = m.get("type")
                    await self.reap()
                    if role is None:
                        candidate = m.get("room", "")
                        challenge = m.get("challenge", "")
                        if not isinstance(candidate, str) or not HEX.fullmatch(candidate):
                            raise ValueError()
                        if not isinstance(challenge, str) or not CHALLENGE.fullmatch(challenge):
                            raise ValueError()
                        if self.limited(request.remote):
                            await self.send(ws, type="error", code="rate_limit")
                            continue
                        if kind == "create":
                            admin = m.get("admin", "")
                            if not isinstance(admin, str) or not HEX.fullmatch(admin) or candidate in self.rooms or len(self.rooms) >= 1000:
                                await self.send(ws, type="error", code="create_failed")
                                continue
                            key, role = candidate, "host"
                            self.rooms[key] = Room(ws, hashlib.sha256(admin.encode()).digest(), challenge)
                            await self.send(ws, type="created")
                        elif kind == "resume":
                            room = self.rooms.get(candidate)
                            admin = m.get("admin", "")
                            if not isinstance(admin, str) or not room or room.host is not None or not hmac.compare_digest(room.admin_hash, hashlib.sha256(admin.encode()).digest()):
                                await self.send(ws, type="error", code="resume_failed")
                                continue
                            key, role = candidate, "host"
                            room.host, room.expiry = ws, None
                            # Rotate host challenge and sessions, so stale traffic cannot be replayed.
                            room.challenge = challenge
                            await self.send(ws, type="created")
                            for pid, guest in room.guests.items():
                                guest.consent = guest.host_consent = guest.turn_issued = False
                                await self.send(ws, type="waiting", peer=pid)
                                if guest.approved:
                                    await self.ready(room, pid)
                        elif kind == "join":
                            room = self.rooms.get(candidate)
                            if not room or room.host is None or len(room.guests) >= 16:
                                await self.send(ws, type="error", code="room_unavailable")
                                continue
                            key, role, peer_id = candidate, "guest", secrets.token_hex(8)
                            room.guests[peer_id] = Guest(ws, challenge)
                            await self.send(ws, type="joined", peer=peer_id)
                            await self.send(room.host, type="waiting", peer=peer_id)
                        else:
                            raise ValueError()
                        continue
                    room = self.rooms.get(key)
                    if room is None:
                        break
                    if role == "host":
                        if kind == "end":
                            await self.close_room(key)
                            break
                        target = m.get("peer")
                        guest = room.guests.get(target) if isinstance(target, str) else None
                        if not guest:
                            await self.send(ws, type="error", code="unknown_peer")
                            continue
                        if kind == "approve":
                            if not guest.approved and sum(g.approved for g in room.guests.values()) >= 4:
                                await self.send(ws, type="error", code="room_full")
                            elif not guest.approved:
                                guest.approved = True
                                await self.ready(room, target)
                        elif kind == "remove":
                            room.guests.pop(target)
                            await self.send(guest.ws, type="ended")
                            await guest.ws.close()
                            await self.send(ws, type="left", peer=target)
                        elif kind == "signal" and guest.approved:
                            await self.forward(room, target, m, True)
                        elif kind == "relay" and guest.approved:
                            guest.host_consent = m.get("enabled") is True
                            await self.turn(room, target)
                        else:
                            raise ValueError()
                    else:
                        guest = room.guests.get(peer_id)
                        if not guest:
                            break
                        if kind == "signal" and guest.approved:
                            await self.forward(room, peer_id, m, False)
                        elif kind == "relay" and guest.approved:
                            guest.consent = m.get("enabled") is True
                            await self.turn(room, peer_id)
                        else:
                            raise ValueError()
                except (ValueError, TypeError, KeyError):
                    await self.send(ws, type="error", code="invalid_message")
        finally:
            room = self.rooms.get(key)
            if room:
                if role == "host" and room.host is ws:
                    room.host = None
                    room.expiry = self.clock() + self.grace
                    for guest in room.guests.values():
                        await self.send(guest.ws, type="host_offline")
                elif role == "guest" and peer_id in room.guests:
                    room.guests.pop(peer_id)
                    await self.send(room.host, type="left", peer=peer_id)
        return ws

    async def ready(self, room, pid):
        guest = room.guests[pid]
        guest.session = secrets.token_hex(16)
        await self.send(guest.ws, type="ready", peer=pid, session=guest.session, challenge=room.challenge)
        await self.send(room.host, type="ready", peer=pid, session=guest.session, challenge=guest.challenge)

    async def forward(self, room, pid, m, from_host):
        guest = room.guests[pid]
        payload, mac = m.get("payload"), m.get("mac")
        if not isinstance(payload, str) or len(payload) > 120000 or not isinstance(mac, str) or not HEX.fullmatch(mac):
            raise ValueError()
        await self.send(guest.ws if from_host else room.host, type="signal", peer=pid, payload=payload, mac=mac)

    async def turn(self, room, pid):
        guest = room.guests[pid]
        if not (guest.consent and guest.host_consent) or guest.turn_issued:
            return
        if not self.turn_secret or not self.turn_host:
            for ws in (room.host, guest.ws):
                await self.send(ws, type="error", code="turn_unavailable", peer=pid)
            return
        guest.turn_issued = True
        username = f"{int(time.time()) + 3600}:{secrets.token_hex(8)}"
        password = base64.b64encode(hmac.new(self.turn_secret.encode(), username.encode(), hashlib.sha1).digest()).decode()
        for ws in (room.host, guest.ws):
            await self.send(ws, type="turn", peer=pid, username=username, password=password,
                            host=self.turn_host, expires=3600)


def application(service=None):
    service = service or Service(turn_secret=os.getenv("TURN_SECRET", ""), turn_host=os.getenv("TURN_HOST", ""))
    app = web.Application(client_max_size=131072)
    app.router.add_get("/ws", service.websocket)
    async def health(_):
        return web.json_response({"ok": True})
    app.router.add_get("/health", health)

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
    app.cleanup_ctx.append(sweep)
    return app


if __name__ == "__main__":
    # TLS terminated by the supplied proxy; bind locally outside containers.
    web.run_app(application(), host=os.getenv("BIND", "127.0.0.1"), port=int(os.getenv("PORT", "8080")),
                access_log=None, print=None)
