"""Lazarus Share Room Client for Discord Bot coordination."""
import asyncio
import base64
import hashlib
import json
import logging
import secrets
from typing import Callable, Optional, Dict, Any, List
import aiohttp

logger = logging.getLogger("lazarus.bot.client")


def generate_secret() -> bytes:
    """Generates 16 random cryptographic bytes for the room secret."""
    return secrets.token_bytes(16)


def token_from_secret(secret: bytes) -> str:
    """Encodes a 16-byte secret into a 26-character Base32 string (without padding)."""
    return base64.b32encode(secret).decode("ascii").rstrip("=")


def secret_from_token(token: str) -> Optional[bytes]:
    """Decodes a 26-character Base32 string into 16-byte secret."""
    normalized = token.strip().upper()
    if len(normalized) != 26:
        return None
    # Add padding characters (6 '=') to reach 32 characters for standard RFC4648 Base32
    padded = normalized + "=" * 6
    try:
        raw = base64.b32decode(padded)
        return raw if len(raw) == 16 else None
    except Exception:
        return None


def room_id_from_secret(secret: bytes) -> str:
    """Calculates SHA-256 room identifier matching Protocol::room in C++."""
    return hashlib.sha256(b"lazarus-share/room/v1:" + secret).hexdigest()


class LazarusRoom:
    """Manages an active room lifecycle on the Lazarus Share signaling server."""

    def __init__(
        self,
        signal_url: str,
        secret: Optional[bytes] = None,
        on_state_change: Optional[Callable[["LazarusRoom"], Any]] = None,
        on_close: Optional[Callable[["LazarusRoom"], Any]] = None,
    ):
        self.signal_url = signal_url
        self.secret = secret or generate_secret()
        self.token = token_from_secret(self.secret)
        self.room_id = room_id_from_secret(self.secret)
        self.admin = secrets.token_hex(32)
        self.challenge = secrets.token_hex(16)

        self.on_state_change = on_state_change
        self.on_close = on_close

        self.peer_id: Optional[str] = None
        self.participants: List[Dict[str, Any]] = []
        self.broadcaster: str = ""
        self.sharing: bool = False
        self.revision: int = 0
        self.owner_online: bool = False
        self.active: bool = False

        self._ws: Optional[aiohttp.ClientWebSocketResponse] = None
        self._session: Optional[aiohttp.ClientSession] = None
        self._task: Optional[asyncio.Task] = None
        self._closing: bool = False

    @property
    def human_participants(self) -> List[Dict[str, Any]]:
        """List of active participants excluding coordinators."""
        return [p for p in self.participants if not p.get("coordinator", False) and p.get("approved", False)]

    @property
    def broadcaster_name(self) -> Optional[str]:
        """Name of current broadcaster if broadcasting."""
        if not self.sharing or not self.broadcaster:
            return None
        for p in self.participants:
            if p.get("peer") == self.broadcaster:
                return p.get("profile", {}).get("nickname") or "Transmissor"
        return "Transmissor"

    async def connect(self) -> None:
        """Connects to the signaling server and creates the room as coordinator."""
        self._session = aiohttp.ClientSession()
        try:
            self._ws = await self._session.ws_connect(self.signal_url, heartbeat=20)
        except Exception as err:
            await self._cleanup()
            raise ConnectionError(f"Failed to connect to signaling server at {self.signal_url}: {err}") from err

        # Send create message
        create_msg = {
            "protocol": 2,
            "type": "create",
            "room": self.room_id,
            "admin": self.admin,
            "challenge": self.challenge,
            "requireApproval": False,
            "coordinator": True,
            "profile": {"nickname": "Discord Bot", "avatar": 0},
            "capabilities": ["profile", "sharing"],
        }
        await self._ws.send_json(create_msg)

        # Wait for created confirmation
        resp = await self._ws.receive_json()
        if resp.get("type") == "created" and resp.get("protocol") == 2:
            self.peer_id = resp.get("peer")
            self.active = True
            logger.info("Room %s created successfully on signaling server (peer %s)", self.room_id[:12], self.peer_id)
        else:
            await self._cleanup()
            raise RuntimeError(f"Failed to create room: {resp}")

        self._task = asyncio.create_task(self._listen_loop())

    async def _listen_loop(self) -> None:
        """Receives room-state and ended messages from the server."""
        try:
            async for msg in self._ws:
                if msg.type == aiohttp.WSMsgType.TEXT:
                    try:
                        data = json.loads(msg.data)
                    except ValueError:
                        continue
                    mtype = data.get("type")
                    if mtype == "room-state":
                        self.participants = data.get("participants", [])
                        self.broadcaster = data.get("broadcaster", "")
                        self.sharing = bool(data.get("sharing", False))
                        self.revision = int(data.get("revision", 0))
                        self.owner_online = bool(data.get("ownerOnline", True))
                        if self.on_state_change:
                            try:
                                res = self.on_state_change(self)
                                if asyncio.iscoroutine(res):
                                    await res
                            except Exception as e:
                                logger.exception("Error in on_state_change callback: %s", e)
                    elif mtype == "ended":
                        logger.info("Room %s ended by server", self.room_id[:12])
                        break
                elif msg.type in (aiohttp.WSMsgType.CLOSED, aiohttp.WSMsgType.ERROR):
                    break
        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.warning("Room %s listen loop error: %s", self.room_id[:12], e)
        finally:
            self.active = False
            if not self._closing and self.on_close:
                try:
                    res = self.on_close(self)
                    if asyncio.iscoroutine(res):
                        await res
                except Exception as e:
                    logger.exception("Error in on_close callback: %s", e)

    async def close(self) -> None:
        """Closes the room explicitly sending 'end' to signaling server."""
        self._closing = True
        self.active = False
        if self._ws and not self._ws.closed:
            try:
                await self._ws.send_json({"type": "end"})
            except Exception:
                pass
        if self._task and not self._task.done():
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
        await self._cleanup()
        if self.on_close:
            try:
                res = self.on_close(self)
                if asyncio.iscoroutine(res):
                    await res
            except Exception as e:
                logger.exception("Error in on_close callback: %s", e)

    async def _cleanup(self) -> None:
        if self._ws and not self._ws.closed:
            await self._ws.close()
        self._ws = None
        if self._session and not self._session.closed:
            await self._session.close()
        self._session = None
