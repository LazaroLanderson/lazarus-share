"""Run synthetic audio tests in a private PipeWire server with NO hardware."""
import os
from pathlib import Path
import subprocess
import tempfile
import time
import json

with tempfile.TemporaryDirectory(prefix="lazarus-audio-test-") as directory:
    root = Path(directory)
    config = root / "pipewire.conf"
    config.write_text('''
context.properties = { core.daemon = true core.name = pipewire-0 default.clock.rate = 48000 }
context.spa-libs = { audio.convert.* = audioconvert/libspa-audioconvert support.* = support/libspa-support }
context.modules = [
  { name = libpipewire-module-protocol-native }
  { name = libpipewire-module-metadata }
  { name = libpipewire-module-spa-node-factory }
  { name = libpipewire-module-client-node }
  { name = libpipewire-module-adapter }
  { name = libpipewire-module-link-factory }
  { name = libpipewire-module-access }
]
context.objects = [
  { factory = spa-node-factory args = { factory.name = support.node.driver node.name = Dummy-Driver node.group = pipewire.dummy node.always-process = true priority.driver = 20000 } }
  { factory = adapter args = { factory.name = support.null-audio-sink node.name = lazarus-test-sink node.always-process = true media.class = Audio/Sink audio.position = [ FL FR ] adapter.auto-port-config = { mode = dsp monitor = true position = preserve } } }
]
''')
    env = os.environ | {"XDG_RUNTIME_DIR": directory, "PIPEWIRE_RUNTIME_DIR": directory,
                        "PIPEWIRE_REMOTE": "pipewire-0", "XDG_STATE_HOME": str(root / "state")}
    processes = []
    try:
        processes.append(subprocess.Popen(["pipewire", "-c", str(config)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        for _ in range(100):
            if (root / "pipewire-0").exists(): break
            time.sleep(.02)
        # Built-in policy profile loads linking but no ALSA, Bluetooth or cameras.
        processes.append(subprocess.Popen(["wireplumber", "--profile=policy"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        time.sleep(1)
        test = subprocess.Popen(["build/audio-test"], env=env)
        time.sleep(3)
        snapshot = subprocess.check_output(["pw-dump"], env=env)
        test.wait(timeout=15)
        result = test
        if result.returncode:
            for item in json.loads(snapshot):
                if item["type"] in ("PipeWire:Interface:Node", "PipeWire:Interface:Client", "PipeWire:Interface:Link"):
                    props = item.get("info", {}).get("props", {})
                    print(item["id"], item.get("info", {}).get("state"), {k: v for k, v in props.items() if k in ("application.name", "application.process.id", "object.serial", "node.name", "media.class", "target.object", "stream.capture.sink", "link.input.node", "link.output.node", "link.passive")})
    finally:
        for process in reversed(processes):
            process.terminate()
            try: process.wait(timeout=3)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
    raise SystemExit(result.returncode)
