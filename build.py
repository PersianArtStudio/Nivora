from pathlib import Path
import struct
import os
import subprocess
import shutil
import sys
import hashlib
from build_tools import resolve_zig

root = Path(__file__).resolve().parent
os.chdir(root)
output = root / "dist"
output.mkdir(exist_ok=True)
zig = resolve_zig(root)
subprocess.run([zig, "rc", "/fo", "dist/resources.res", "Nivora.rc"], check=True)
exe = output / "Nivora.exe"
subprocess.run([zig, "cc", "-target", "x86_64-windows-gnu", "-Os", "-s",
                "-municode", "-Wl,--subsystem,windows", "nivora.c", "dist/resources.res",
                "-lws2_32", "-lshell32", "-lcomdlg32", "-ladvapi32", "-lgdi32", "-lole32",
                "-o", str(exe)], check=True)
pe = exe.read_bytes()
header = struct.unpack_from("<I", pe, 0x3c)[0]
if pe[header:header+4] != b"PE\0\0" or struct.unpack_from("<H", pe, header+4)[0] != 0x8664 or struct.unpack_from("<H", pe, header+24+68)[0] != 2:
    raise RuntimeError("Expected a Windows x64 GUI executable")
if "--installer" in sys.argv:
    candidates = [os.environ.get("ISCC"), shutil.which("ISCC")]
    for variable in ("ProgramFiles(x86)", "ProgramFiles", "LOCALAPPDATA"):
        base = os.environ.get(variable)
        if base:
            candidates.extend(str(Path(base) / folder / "ISCC.exe")
                              for folder in ("Inno Setup 6", "Programs/Inno Setup 6"))
    compiler = next((value for value in candidates if value and Path(value).is_file()), None)
    if not compiler:
        sys.exit("Install Inno Setup 6 or set ISCC to its compiler executable.")
    subprocess.run([compiler, "Nivora.iss"], check=True)
    installer = output / "Nivora-Setup-1.1.0-x64.exe"
    digest = hashlib.sha256(installer.read_bytes()).hexdigest()
    (output / "SHA256SUMS.txt").write_text(f"{digest}  {installer.name}\n", encoding="ascii")
    print(installer)
