#!/usr/bin/env python3
"""Build an AppDir, copying only the runtime plugins used by this application."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

p = argparse.ArgumentParser()
p.add_argument("--binary", default="build/lazarus-share")
p.add_argument("--updater", default="build/lazarus-updater")
p.add_argument("--output", default="dist/LazarusShare.AppDir")
p.add_argument("--qt-plugins", default="/usr/lib/x86_64-linux-gnu/qt6/plugins")
a = p.parse_args()
root = Path(a.output).resolve()
if root.exists(): shutil.rmtree(root)
root.mkdir(parents=True, exist_ok=True)
binary = root / "usr/bin/lazarus-share"; binary.parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(a.binary, binary)
# This runtime is copied out of the AppImage before the old mount can disappear.
updater_runtime = root / "usr/libexec/update-runtime"
updater_runtime.mkdir(parents=True, exist_ok=True)
shutil.copy2(a.updater, updater_runtime / "lazarus-updater")
helper_queue = [updater_runtime / "lazarus-updater"]
helper_seen = set()
helper_sources = set()
while helper_queue:
    item = helper_queue.pop()
    if item in helper_seen: continue
    helper_seen.add(item)
    output = subprocess.check_output(["ldd", str(item)], text=True)
    if "not found" in output: raise SystemExit(f"Missing updater dependency: {output}")
    for name, source in re.findall(r"\s+(\S+) => (/\S+)", output):
        if re.match(r"^(ld-linux|libc\.so|libm\.so|libpthread\.so|libdl\.so|librt\.so|libresolv\.so)", name): continue
        destination = updater_runtime / name
        if not destination.exists():
            shutil.copy2(source, destination); helper_queue.append(destination)
            helper_sources.add(Path(source).resolve())
libraries = root / "usr/lib"; libraries.mkdir(parents=True, exist_ok=True)
queue = [binary]; seen = set(); bundled_sources = helper_sources.copy()

def copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination); queue.append(destination)
    bundled_sources.add(Path(source).resolve())

scanner = Path("/usr/lib/x86_64-linux-gnu/gstreamer1.0/gstreamer-1.0/gst-plugin-scanner")
copy(scanner, root / "usr/libexec/gst-plugin-scanner")

for element in ("queue", "appsrc", "webrtcbin", "nicesrc", "dtlssrtpenc", "srtpenc", "rtpbin", "rtpvp8pay", "vp8enc", "opusenc", "audioconvert", "audioresample", "volume", "audiomixer", "audiotestsrc", "videotestsrc", "videoconvert", "videoscale", "videorate", "pipewiresrc", "ximagesrc", "autoaudiosink", "pulsesink", "h264parse", "openh264dec", "decodebin"):
    details = subprocess.check_output(["gst-inspect-1.0", element], text=True)
    source = Path(re.search(r"Filename\s+(\S+)", details)[1])
    destination = libraries / "gstreamer-1.0" / source.name
    if not destination.exists(): copy(source, destination)

# Factories are registered against the target GPU at runtime, even when the
# build machine has no GPU. Bundle plugins by filename, not factory discovery.
for name in ("va", "nvcodec", "qsv"):
    source = Path("/usr/lib/x86_64-linux-gnu/gstreamer-1.0") / f"libgst{name}.so"
    if source.exists(): copy(source, libraries / "gstreamer-1.0" / source.name)

for group in ("platforms", "platforminputcontexts", "wayland-shell-integration", "wayland-decoration-client", "wayland-graphics-integration-client", "xcbglintegrations", "tls"):
    directory = Path(a.qt_plugins) / group
    if directory.exists():
        for source in directory.glob("*.so"): copy(source, root / "usr/plugins" / group / source.name)

pipewire = Path("/usr/lib/x86_64-linux-gnu/pipewire-0.3")
for source in pipewire.glob("libpipewire-module-*.so"):
    copy(source, libraries / "pipewire-0.3" / source.name)

spa = Path("/usr/lib/x86_64-linux-gnu/spa-0.2")
for source in spa.rglob("*.so"): copy(source, libraries / "spa-0.2" / source.relative_to(spa))

# Keep glibc, GPU drivers and loader owned by the host; bundle other dependencies.
excluded = re.compile(r"^(ld-linux|libc\.so|libm\.so|libpthread\.so|libdl\.so|librt\.so|libresolv\.so|libGLX|libEGL|libGLdispatch|libGL\.so|libOpenGL|libdrm|libvulkan|libgbm|libnvidia)")
while queue:
    item = queue.pop()
    if item in seen: continue
    seen.add(item)
    output = subprocess.check_output(["ldd", str(item)], text=True)
    if "not found" in output: raise SystemExit(f"Missing runtime dependency in {item}:\n{output}")
    for name, source in re.findall(r"\s+(\S+) => (/\S+)", output):
        if excluded.match(name): continue
        destination = libraries / name
        if not destination.exists(): copy(source, destination)

repo = Path(__file__).resolve().parents[2]
for name in ("AppRun", "lazarus-share.desktop", "lazarus-share.svg"):
    shutil.copy2(repo / "packaging/linux" / name, root / name)
(root / "AppRun").chmod(0o755)
if (repo / "assets").exists():
    shutil.copytree(repo / "assets", root / "usr/bin/assets", dirs_exist_ok=True)
    shutil.copytree(repo / "assets", root / "assets", dirs_exist_ok=True)
license_dir = root / "usr/share/licenses"; license_dir.mkdir(parents=True, exist_ok=True)
shutil.copy2(repo / "LICENSE", license_dir / "GPL-3.0.txt")
shutil.copytree(repo / "docs/licenses", license_dir / "dependencies", dirs_exist_ok=True)
# Include distribution copyright notices for every library/plugin we ship.
result = subprocess.run(["dpkg-query", "-S", *map(str, sorted(bundled_sources))], capture_output=True, text=True)
for line in result.stdout.splitlines():
    if ": " not in line: continue
    package = line.split(": ", 1)[0].split(":", 1)[0]
    notice = Path("/usr/share/doc") / package / "copyright"
    if notice.exists(): shutil.copy2(notice, license_dir / (package + ".copyright"))
print(root)
