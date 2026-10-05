"""Receive synthetic WebRTC media using only the bundled Windows runtime."""
import os
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
runtime = repo / "dist/windows-app"
with tempfile.TemporaryDirectory(prefix="lazarus-media-") as state:
    system = Path(os.environ["SystemRoot"])
    env = os.environ | {
        "PATH": os.pathsep.join(map(str, [runtime, system / "System32", system])),
        "GST_PLUGIN_SYSTEM_PATH_1_0": "",
        "GST_PLUGIN_PATH_1_0": str(runtime / "gstreamer-1.0"),
        "GST_PLUGIN_SCANNER_1_0": str(runtime / "gst-plugin-scanner.exe"),
        "GST_REGISTRY_1_0": str(Path(state) / "registry.bin"),
        "GST_DEBUG": "0",
        "LAZARUS_VIDEO_ENCODER": "vp8",
        "LAZARUS_VIDEO_DECODER": "software",
    }
    env.pop("LAZARUS_TEST_H264", None)
    for arguments in (["--h264", "--require-software-decoder"], ["--h264", "--decoder-failure"], ["--audio-failure"], ["--audio-flow-failure"], ["--audio-start-failure"]):
        subprocess.run([str(repo / "build/media-test.exe"), *arguments], env=env, check=True, timeout=30)
print("Bundled Windows H.264 reception and receiver error handling passed")
