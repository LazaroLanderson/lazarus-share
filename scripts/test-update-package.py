#!/usr/bin/env python3
"""Exercise the actual portable package and its independently copied updater runtime."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import tempfile
import time
import uuid

parser = argparse.ArgumentParser()
parser.add_argument("package", type=Path)
parser.add_argument("runtime", type=Path)
args = parser.parse_args()
windows = os.name == "nt"
version = re.search(r"project\(LazarusShare VERSION ([0-9.]+)", Path("CMakeLists.txt").read_text())[1]

# A real Windows startup registers its invitation handler; save and restore only that key.
registry = None
if windows:
    import winreg
    registry_path = r"Software\Classes\lazarus-share"

    def snapshot(path):
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
                values, children = [], {}
                for i in range(winreg.QueryInfoKey(key)[1]):
                    values.append(winreg.EnumValue(key, i))
                for i in range(winreg.QueryInfoKey(key)[0]):
                    name = winreg.EnumKey(key, i)
                    children[name] = snapshot(path + "\\" + name)
                return values, children
        except FileNotFoundError:
            return None

    def restore(path, saved):
        current = snapshot(path)
        if current:
            for name in current[1]:
                restore(path + "\\" + name, None)
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, path)
        if saved is not None:
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER, path) as key:
                for name, value, kind in saved[0]:
                    winreg.SetValueEx(key, name, 0, kind, value)
            for name, child in saved[1].items():
                restore(path + "\\" + name, child)

    registry = snapshot(registry_path)

helper = None
child_pid = 0
transaction = None
try:
    with tempfile.TemporaryDirectory(prefix="lazarus-package-update-") as scratch:
        scratch = Path(scratch)
        env = os.environ | {"QT_QPA_PLATFORM": "windows" if windows else "offscreen", "GST_DEBUG": "0",
            "XDG_DATA_HOME": str(scratch / "data"), "XDG_CONFIG_HOME": str(scratch / "config"),
            "XDG_CACHE_HOME": str(scratch / "cache")}
        if windows:
            # The copied helper and package must not rely on the development runtime in PATH.
            env["PATH"] = str(Path(os.environ["SystemRoot"]) / "System32")
        for key in ("APPIMAGE", "APPDIR", "LAZARUS_LAUNCHER_PATH", "LAZARUS_LAUNCHER_PID",
                    "LAZARUS_UPDATE_TRANSACTION", "LAZARUS_UPDATE_ROLLBACK", "LAZARUS_UPDATE_JOB"):
            env.pop(key, None)
        update_root = (Path(os.environ["LOCALAPPDATA"]) if windows else scratch / "data") / "LazarusLabs/LazarusShare/updates"
        uid = str(uuid.uuid4())
        transaction = update_root / uid
        transaction.mkdir(parents=True, mode=0o700)
        runtime = transaction / "runtime"
        shutil.copytree(args.runtime, runtime)
        target = scratch / ("Portable app.exe" if windows else "Portable app.AppImage")
        shutil.copy2(args.package, target)
        staged = scratch / (".lazarus-update-" + uid + ".new")
        backup = scratch / (".lazarus-update-" + uid + ".old")
        shutil.copy2(args.package, staged)
        sentinel = scratch / "keep me.txt"
        sentinel.write_text("persistent user file")
        journal = transaction / "transaction.json"
        journal.write_text(json.dumps({"id": uid, "target": target.as_posix(), "staged": staged.as_posix(),
            "backup": backup.as_posix(), "size": staged.stat().st_size,
            "sha256": hashlib.sha256(staged.read_bytes()).hexdigest(), "version": version,
            "state": "prepared", "appPid": 0, "launcherPid": 0, "extract": not windows}))
        if not windows:
            env["LD_LIBRARY_PATH"] = str(runtime)
            # Confirm that profile persistence is untouched by a real startup after replacement.
            profile = scratch / "config/LazarusLabs/LazarusShare.conf"
            profile.parent.mkdir(parents=True)
            profile.write_text("[profile]\nnickname=UpdateTest\navatar=3\nrelay=true\n")
            original_profile = profile.read_bytes()
        program = runtime / ("lazarus-updater.exe" if windows else "lazarus-updater")
        helper = subprocess.Popen([str(program), str(journal)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.monotonic() + 80
        while time.monotonic() < deadline:
            value = json.loads(journal.read_text())
            child_pid = int(value.get("childPid", 0))
            if value["state"] == "completed":
                assert (transaction / "ack.json").is_file(), "Package did not confirm startup"
                if not staged.exists() and not backup.exists():
                    break
            if helper.poll() is not None:
                raise RuntimeError("Package update exited before confirmation")
            time.sleep(0.1)
        else:
            raise RuntimeError("Package update confirmation timed out")
        assert sentinel.read_text() == "persistent user file"
        assert target.is_file() and not staged.exists() and not backup.exists()
        if not windows:
            assert profile.read_bytes() == original_profile
        if child_pid:
            if windows:
                subprocess.run(["taskkill", "/PID", str(child_pid), "/T", "/F"], check=True, stdout=subprocess.DEVNULL)
            else:
                os.killpg(child_pid, signal.SIGTERM)
        helper.wait(timeout=15)
        assert helper.returncode == 0
        print("Real portable package replacement, startup confirmation, persistent data and safe cleanup passed")
finally:
    if helper is not None and helper.poll() is None:
        if child_pid and not windows:
            try:
                os.killpg(child_pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        helper.kill()
        helper.wait(timeout=10)
    if transaction is not None:
        shutil.rmtree(transaction, ignore_errors=True)
    if windows:
        restore(registry_path, registry)
