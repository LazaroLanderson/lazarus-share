#!/usr/bin/env python3
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import zipfile
import os
import sys
from verify_dependencies import verify

p = argparse.ArgumentParser()
p.add_argument("--prefix", default=str(Path(sys.executable).resolve().parents[1]))
p.add_argument("--binary", default="build/lazarus-share.exe")
p.add_argument("--updater", default="build/lazarus-updater.exe")
p.add_argument("--tool-prefix", default="", help="Cross tools prefix, e.g. /path/x86_64-w64-mingw32-")
a = p.parse_args(); prefix = Path(a.prefix)
def tool(name):
    return a.tool_prefix + name if a.tool_prefix else str(prefix / "bin" / (name + ".exe"))
repo = Path(__file__).resolve().parents[2]
dist = repo / "dist"; app = dist / "windows-app"
if app.exists(): shutil.rmtree(app)
app.mkdir(parents=True, exist_ok=True)
shutil.copy2(a.binary, app / "lazarus-share.exe")
if not a.tool_prefix:
    subprocess.run([tool("windeployqt6"), "--release", "--no-translations", str(app / "lazarus-share.exe")], check=True)
else:
    for group in ("platforms", "tls"):
        shutil.copytree(prefix / "share/qt6/plugins" / group, app / group, dirs_exist_ok=True)
shutil.copy2(a.updater, app / "lazarus-updater.exe")
queue = list(app.rglob("*.dll")) + [app / "lazarus-share.exe", app / "lazarus-updater.exe"]
scanner = prefix / "libexec/gstreamer-1.0/gst-plugin-scanner.exe"
if scanner.exists():
    shutil.copy2(scanner, app / scanner.name); queue.append(app / scanner.name)
plugins = app / "gstreamer-1.0"; plugins.mkdir(exist_ok=True)
for plugin in ("coreelements", "app", "webrtc", "nice", "dtls", "srtp", "rtp", "rtpmanager", "vpx", "opus", "audioconvert", "audioresample", "volume", "audiomixer", "audiotestsrc", "videotestsrc", "videoconvertscale", "videorate", "d3d11", "wasapi2", "autodetect", "playback", "videoparsersbad", "openh264", "nvcodec", "qsv"):
    matches = list((prefix / "lib/gstreamer-1.0").glob(f"*gst{plugin}.dll"))
    if not matches: raise SystemExit(f"Required GStreamer plugin missing: {plugin}")
    target = plugins / matches[0].name; shutil.copy2(matches[0], target); queue.append(target)
seen = set()
missing = set()
while queue:
    item = queue.pop()
    if item in seen: continue
    seen.add(item)
    output = subprocess.check_output([tool("objdump"), "-p", str(item)], text=True)
    for dll in re.findall(r"DLL Name:\s*(\S+)", output):
        system = {"kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "shell32.dll", "ole32.dll", "oleaut32.dll", "ws2_32.dll", "winmm.dll", "msvcrt.dll", "ntdll.dll", "bcrypt.dll", "crypt32.dll", "secur32.dll", "dnsapi.dll", "iphlpapi.dll", "psapi.dll", "d3d9.dll", "d3d11.dll", "dxgi.dll", "d3dcompiler_47.dll", "dwmapi.dll", "shlwapi.dll", "setupapi.dll", "imm32.dll", "uxtheme.dll", "version.dll", "normaliz.dll", "netapi32.dll", "mpr.dll", "comdlg32.dll", "winspool.drv", "opengl32.dll", "dwrite.dll", "d2d1.dll", "dhcpcsvc.dll", "mfplat.dll", "mf.dll", "mfuuid.dll", "propsys.dll", "winhttp.dll", "wtsapi32.dll", "userenv.dll", "cfgmgr32.dll", "hid.dll", "powrprof.dll", "dcomp.dll", "ucrtbase.dll", "msvcp_win.dll", "win32u.dll", "dbghelp.dll", "combase.dll", "wintrust.dll", "cryptbase.dll", "ncrypt.dll", "d3d12.dll", "dxcore.dll", "glu32.dll", "msimg32.dll", "mswsock.dll", "msasn1.dll", "shcore.dll", "usp10.dll", "mscms.dll", "faultrep.dll", "dinput8.dll", "ddraw.dll", "avrt.dll", "mfreadwrite.dll", "ksuser.dll", "cabinet.dll", "mmdevapi.dll", "rpcrt4.dll", "wsock32.dll", "wininet.dll", "msvfw32.dll", "vfw32.dll", "authz.dll"}
        if dll.lower() in system or dll.lower().startswith(("api-ms-", "ext-ms-")): continue
        source = prefix / "bin" / dll
        if source.exists() and not (app / dll).exists():
            shutil.copy2(source, app / dll); queue.append(app / dll)
        elif not source.exists():
            # These are provided by supported Windows versions. Missing non-system
            # DLLs must fail packaging rather than produce a broken executable.
            if dll.lower() not in system and not dll.lower().startswith(("api-ms-", "ext-ms-")):
                missing.add(dll)
if missing: raise SystemExit("Missing non-system DLLs: " + ", ".join(sorted(missing)))
verify(app, tool("objdump"), system)
shutil.copy2(repo / "LICENSE", app / "LICENSE")
shutil.copytree(repo / "docs/licenses", app / "licenses", dirs_exist_ok=True)
# MSYS2's runtime packages provide dependency notices in share/licenses.
if (prefix / "share/licenses").exists():
    shutil.copytree(prefix / "share/licenses", app / "licenses/upstream", dirs_exist_ok=True)
# Qt Core helper gets its own transitive DLL closure, without the GUI/media runtime.
runtime = app / "update-runtime"; runtime.mkdir()
shutil.copy2(app / "lazarus-updater.exe", runtime / "lazarus-updater.exe")
helper_queue = [runtime / "lazarus-updater.exe"]
while helper_queue:
    item = helper_queue.pop()
    output = subprocess.check_output([tool("objdump"), "-p", str(item)], text=True)
    for dll in re.findall(r"DLL Name:\s*(\S+)", output):
        if dll.lower() in system or dll.lower().startswith(("api-ms-", "ext-ms-")): continue
        destination = runtime / dll
        if not destination.exists():
            source = app / dll
            if not source.exists(): raise SystemExit(f"Missing updater dependency: {dll}")
            shutil.copy2(source, destination); helper_queue.append(destination)
verify(runtime, tool("objdump"), system)
(app / "lazarus-updater.exe").unlink()
archive = dist / "windows-payload.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
    for file in app.rglob("*"):
        if file.is_file(): z.write(file, file.relative_to(app))
rc = dist / "payload.rc"; rc.write_text(f'1 RCDATA "{archive.as_posix()}"\n')
subprocess.run([tool("windres"), str(rc), "-o", str(dist / "payload.o")], check=True)
subprocess.run([tool("g++"), "-std=c++20", "-O2", "-static", "-municode", "-mwindows", str(repo / "packaging/windows/launcher.cpp"), str(dist / "payload.o"), "-lbcrypt", "-ladvapi32", "-o", str(dist / "LazarusShare.exe")], check=True)
print(dist / "LazarusShare.exe")
