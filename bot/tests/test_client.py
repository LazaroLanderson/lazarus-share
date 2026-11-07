"""Unit tests for Lazarus Share Discord Bot client and protocol helpers."""
import asyncio
import unittest
from aiohttp.test_utils import TestServer, TestClient

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from client import (
    generate_secret,
    token_from_secret,
    secret_from_token,
    room_id_from_secret,
    LazarusRoom,
)
from server.service import Service, application


class TestProtocolHelpers(unittest.TestCase):
    def test_secret_token_roundtrip(self):
        for _ in range(50):
            secret = generate_secret()
            self.assertEqual(len(secret), 16)
            token = token_from_secret(secret)
            self.assertEqual(len(token), 26)
            recovered = secret_from_token(token)
            self.assertEqual(secret, recovered)

    def test_invalid_tokens(self):
        self.assertIsNone(secret_from_token(""))
        self.assertIsNone(secret_from_token("SHORT"))
        self.assertIsNone(secret_from_token("A" * 25))
        self.assertIsNone(secret_from_token("1" * 26))  # 1 is not in RFC 4648 Base32 alphabet

    def test_room_id_derivation(self):
        import hashlib
        secret = b"1234567890123456"
        expected = hashlib.sha256(b"lazarus-share/room/v1:" + secret).hexdigest()
        self.assertEqual(room_id_from_secret(secret), expected)


class TestLazarusRoomIntegration(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.service = Service(clock=lambda: 1000)
        self.server = TestServer(application(self.service))
        self.client = TestClient(self.server)
        await self.client.start_server()
        self.signal_url = f"ws://127.0.0.1:{self.server.port}/ws"

    async def asyncTearDown(self):
        await self.client.close()

    async def test_room_connect_and_callbacks(self):
        state_changes = []
        closed_events = []

        room = LazarusRoom(
            signal_url=self.signal_url,
            on_state_change=lambda r: state_changes.append(r.revision),
            on_close=lambda r: closed_events.append(True),
        )

        await room.connect()
        self.assertTrue(room.active)
        self.assertIsNotNone(room.peer_id)
        self.assertEqual(len(room.token), 26)

        # Connect a human guest to the room created by the bot
        guest_ws = await self.client.ws_connect("/ws")
        await guest_ws.send_json({
            "protocol": 2,
            "type": "join",
            "room": room.room_id,
            "challenge": "a" * 32,
            "profile": {"nickname": "GuestUser", "avatar": 1},
        })
        joined = await guest_ws.receive_json()
        self.assertEqual(joined["type"], "joined")

        # Wait briefly for state change to reach the bot
        await asyncio.sleep(0.1)
        self.assertEqual(len(room.human_participants), 1)
        self.assertEqual(room.human_participants[0]["profile"]["nickname"], "GuestUser")

        # Close the room
        await room.close()
        self.assertFalse(room.active)
        self.assertTrue(len(closed_events) > 0)
        await guest_ws.close()


if __name__ == "__main__":
    unittest.main()
