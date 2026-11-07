"""Discord Bot for Lazarus Share integration.

Commands:
- /share (/lazarus): Creates a room, mentions voice channel members, and sends 1-click join button.
- /encerrar: Ends the current active sharing room.
"""
import os
import sys
import urllib.parse
import logging
from typing import Dict, Optional
import discord
from discord import app_commands
from discord.ext import commands
from dotenv import load_dotenv

from client import LazarusRoom

load_dotenv()

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s"
)
logger = logging.getLogger("lazarus.bot")

DISCORD_BOT_TOKEN = os.getenv("DISCORD_BOT_TOKEN", "")
SIGNAL_URL = os.getenv("SIGNAL_URL", "wss://share.app.lazaruslabs.com.br/ws")
WEB_JOIN_BASE_URL = os.getenv("WEB_JOIN_BASE_URL", "https://share.app.lazaruslabs.com.br/join")
APP_DOWNLOAD_URL = os.getenv("APP_DOWNLOAD_URL", "https://github.com/LazaroLanderson/lazarus-share/releases/latest")
GUILD_ID = os.getenv("DISCORD_GUILD_ID")

intents = discord.Intents.default()
intents.voice_states = True
intents.guilds = True

bot = commands.Bot(command_prefix="!", intents=intents)

# Active rooms indexed by channel_id or user_id
active_rooms: Dict[int, LazarusRoom] = {}
room_messages: Dict[int, discord.Message] = {}
room_voice_channels: Dict[int, int] = {}


def build_invite_url(token: str, nickname: Optional[str] = None) -> str:
    """Builds the full web invitation URL with deep link support."""
    url = f"{WEB_JOIN_BASE_URL}#{token}"
    if nickname:
        quoted = urllib.parse.quote(nickname.strip())
        url += f"&nick={quoted}"
    return url


def create_room_embed(
    host_name: str,
    voice_channel_name: str,
    room: LazarusRoom,
    closed: bool = False
) -> discord.Embed:
    """Builds the rich Discord Embed presenting room status."""
    if closed:
        embed = discord.Embed(
            title="🛑 Lazarus Share — Transmissão Encerrada",
            description=f"A sala iniciada por **{host_name}** para a call **{voice_channel_name}** foi encerrada.",
            color=0xBA1A1A,
        )
        embed.set_footer(text="Lazarus Share • Sessão finalizada")
        return embed

    broadcaster = room.broadcaster_name
    participants_count = len(room.human_participants)

    if broadcaster:
        status_text = f"🟢 **Ao Vivo** — Transmitindo: **{broadcaster}**"
        color = 0x006C4F
    else:
        status_text = "🟡 **Aguardando Transmissão** — Abra o app para compartilhar sua tela"
        color = 0x0FFCBE

    embed = discord.Embed(
        title="📺 Lazarus Share — Transmissão ao Vivo",
        description=(
            f"Uma sala de compartilhamento com latência ultrabaixa foi iniciada para a call **{voice_channel_name}**!\n\n"
            f"{status_text}\n\n"
            f"👥 **Espectadores na sala:** {participants_count} / 5\n\n"
            "Clique no botão abaixo para abrir diretamente o **Lazarus Share** no seu PC."
        ),
        color=color,
    )
    embed.add_field(
        name="Anfitrião",
        value=host_name,
        inline=True,
    )
    embed.add_field(
        name="Canal de Voz",
        value=voice_channel_name,
        inline=True,
    )
    embed.set_footer(text="Lazarus Share • P2P WebRTC 60 FPS")
    return embed


class RoomActionView(discord.ui.View):
    """Buttons attached to the stream message."""

    def __init__(self, join_url: str, closed: bool = False):
        super().__init__(timeout=None)
        if not closed:
            self.add_item(
                discord.ui.Button(
                    label="🚀 Entrar na Transmissão",
                    url=join_url,
                    style=discord.ButtonStyle.link,
                )
            )
            self.add_item(
                discord.ui.Button(
                    label="📥 Baixar Aplicativo",
                    url=APP_DOWNLOAD_URL,
                    style=discord.ButtonStyle.link,
                )
            )
        else:
            disabled_btn = discord.ui.Button(
                label="Transmissão Finalizada",
                style=discord.ButtonStyle.secondary,
                disabled=True,
            )
            self.add_item(disabled_btn)


@bot.event
async def on_ready():
    logger.info("Lazarus Discord Bot connected as %s (ID: %s)", bot.user, bot.user.id)
    try:
        if GUILD_ID:
            guild = discord.Object(id=int(GUILD_ID))
            bot.tree.copy_global_to(guild=guild)
            synced = await bot.tree.sync(guild=guild)
            logger.info("Synced %d commands to guild %s", len(synced), GUILD_ID)
        else:
            synced = await bot.tree.sync()
            logger.info("Synced %d global commands", len(synced))
    except Exception as e:
        logger.exception("Failed to sync commands: %s", e)


@bot.tree.command(name="share", description="Inicia uma sala de transmissão no Lazarus Share com os colegas da chamada")
async def share_command(interaction: discord.Interaction):
    """Starts a Lazarus Share room and invites members of the current voice channel."""
    # 1. Check if user is in a voice channel
    if not interaction.user.voice or not interaction.user.voice.channel:
        await interaction.response.send_message(
            "❌ Você precisa estar conectado a um canal de voz para iniciar a transmissão com seus colegas.",
            ephemeral=True,
        )
        return

    voice_channel = interaction.user.voice.channel
    user_id = interaction.user.id
    vc_id = voice_channel.id

    # Check if this voice channel already has an active room
    if vc_id in active_rooms and active_rooms[vc_id].active:
        existing_room = active_rooms[vc_id]
        join_url = build_invite_url(existing_room.token, interaction.user.display_name)
        view = RoomActionView(join_url)
        await interaction.response.send_message(
            f"⚠️ Já existe uma sala ativa para o canal **{voice_channel.name}**!",
            view=view,
            ephemeral=True,
        )
        return

    await interaction.response.defer()

    # 2. Collect members currently in the voice channel (excluding bots)
    members = [m for m in voice_channel.members if not m.bot and m.id != user_id]
    mentions_text = " ".join(m.mention for m in members) if members else ""

    host_name = interaction.user.display_name

    # 3. Create the Lazarus Room via coordinator WebSocket
    room = None
    try:
        room = LazarusRoom(signal_url=SIGNAL_URL)
        await room.connect()
    except Exception as err:
        logger.exception("Error connecting room: %s", err)
        await interaction.followup.send(
            f"❌ Não foi possível criar a sala no servidor de sinalização: `{err}`",
            ephemeral=True,
        )
        return

    active_rooms[vc_id] = room
    room_voice_channels[vc_id] = vc_id

    join_url = build_invite_url(room.token, host_name)
    embed = create_room_embed(host_name, voice_channel.name, room)
    view = RoomActionView(join_url)

    content = f"📢 {interaction.user.mention} iniciou uma transmissão no **Lazarus Share**!"
    if mentions_text:
        content += f"\n👥 Chamando: {mentions_text}"

    msg = await interaction.followup.send(content=content, embed=embed, view=view)
    room_messages[vc_id] = msg

    # Set up state change updates to update Discord Embed
    async def update_discord_message(r: LazarusRoom):
        try:
            current_msg = room_messages.get(vc_id)
            if current_msg:
                new_embed = create_room_embed(host_name, voice_channel.name, r)
                await current_msg.edit(embed=new_embed, view=RoomActionView(join_url))
        except Exception as e:
            logger.warning("Failed to update Discord embed: %s", e)

    async def on_room_closed(r: LazarusRoom):
        try:
            current_msg = room_messages.pop(vc_id, None)
            active_rooms.pop(vc_id, None)
            room_voice_channels.pop(vc_id, None)
            if current_msg:
                closed_embed = create_room_embed(host_name, voice_channel.name, r, closed=True)
                await current_msg.edit(embed=closed_embed, view=RoomActionView(join_url, closed=True))
        except Exception as e:
            logger.warning("Failed to finalize closed Discord embed: %s", e)

    room.on_state_change = update_discord_message
    room.on_close = on_room_closed


@bot.tree.command(name="encerrar", description="Encerra a sala ativa do Lazarus Share na sua chamada")
async def end_command(interaction: discord.Interaction):
    """Closes the active room for the current voice channel."""
    if not interaction.user.voice or not interaction.user.voice.channel:
        await interaction.response.send_message(
            "❌ Você não está em um canal de voz.",
            ephemeral=True,
        )
        return

    vc_id = interaction.user.voice.channel.id
    room = active_rooms.get(vc_id)

    if not room or not room.active:
        await interaction.response.send_message(
            "ℹ️ Não há nenhuma sala ativa do Lazarus Share neste canal de voz.",
            ephemeral=True,
        )
        return

    await interaction.response.defer(ephemeral=True)
    await room.close()
    await interaction.followup.send("✅ Sala do Lazarus Share encerrada com sucesso!", ephemeral=True)


@bot.event
async def on_voice_state_update(member: discord.Member, before: discord.VoiceState, after: discord.VoiceState):
    """Auto-closes rooms when the voice channel becomes empty."""
    if before.channel and before.channel.id in active_rooms:
        vc = before.channel
        remaining_humans = [m for m in vc.members if not m.bot]
        if not remaining_humans:
            room = active_rooms.get(vc.id)
            if room and room.active:
                logger.info("Voice channel %s is empty. Auto-closing Lazarus room %s", vc.name, room.room_id[:12])
                await room.close()


def main():
    if not DISCORD_BOT_TOKEN:
        logger.warning(
            "DISCORD_BOT_TOKEN não configurado. O serviço do Bot no Discord ficará inativo. "
            "Configure DISCORD_BOT_TOKEN no arquivo .env para ativá-lo."
        )
        import time
        while True:
            time.sleep(3600)
    bot.run(DISCORD_BOT_TOKEN)


if __name__ == "__main__":
    main()
