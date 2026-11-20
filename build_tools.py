"""Resolve the pinned Zig compiler for Nivora's Windows x64 build."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

VERSION = "0.13.0"
WHEEL = "ziglang-0.13.0-py3-none-win_amd64.whl"
SHA256 = "b64b2aa936ebbfeecfbb2611df331a5474edebe1246b98c2f56d6ae4d58b71fc"


def correct_version(executable):
    if not executable:
        return False
    try:
        result = subprocess.run([str(executable), "version"], capture_output=True,
                                text=True, timeout=20)
        return result.returncode == 0 and result.stdout.strip() == VERSION
    except (OSError, subprocess.SubprocessError):
        return False


def unpack(wheel, destination):
    digest = hashlib.sha256()
    with wheel.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != SHA256:
        raise SystemExit("Zig download failed SHA-256 verification. Replace the wheel and retry.")
    with zipfile.ZipFile(wheel) as archive:
        archive.extractall(destination)


def resolve_zig(root):
    cache = root / ".build-tools"
    executable = cache / ("zig-" + VERSION) / "ziglang" / "zig.exe"
    for candidate in (os.environ.get("ZIG"), executable, shutil.which("zig")):
        if correct_version(candidate):
            return str(candidate)
    if sys.platform != "win32":
        raise SystemExit("For cross-compilation, set ZIG to the Zig 0.13.0 compiler.")
    cache.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=cache) as folder:
        staging = Path(folder)
        wheel = root / WHEEL
        if not wheel.is_file():
            print("Downloading the Windows x64 Zig compiler (80 MB)...", flush=True)
            command = [sys.executable, "-m", "pip", "download", "--disable-pip-version-check",
                       "--only-binary=:all:", "--no-deps", "--platform", "win_amd64",
                       "--dest", str(staging), "ziglang==" + VERSION]
            if subprocess.run(command).returncode:
                raise SystemExit("Download failed. Check your Internet or put " + WHEEL +
                                 " next to Build-Installer.bat for an offline build.")
            wheel = staging / WHEEL
        unpack(wheel, staging / "package")
        if not correct_version(staging / "package" / "ziglang" / "zig.exe"):
            raise SystemExit("Windows could not run the verified x64 Zig compiler.")
        install = executable.parents[1]
        if install.exists():
            if correct_version(executable):
                return str(executable)
            shutil.rmtree(install)
        (staging / "package").rename(install)
    return str(executable)
