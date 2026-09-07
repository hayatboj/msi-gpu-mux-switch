#!/usr/bin/env python3
"""Stage a CMake build and create a checksum-verified offline release bundle."""
import hashlib
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

sys.dont_write_bytecode = True
source = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(source / "packaging"))
from managed_files import FILES

version, build_arg, output_arg = sys.argv[1:]
if sys.platform != "linux" or platform.machine() != "x86_64":
    raise SystemExit("This release bundle must be built on Linux x86_64")
if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.]+)?", version):
    raise SystemExit("Invalid release version")
build, output = Path(build_arg).resolve(), Path(output_arg).resolve()
output.mkdir(parents=True, exist_ok=True)
name = f"msi-mux-{version}-linux-x86_64"
archive = output / f"{name}.tar.gz"
if archive.exists():
    raise SystemExit(f"Refusing to overwrite an existing archive: {archive}")
with tempfile.TemporaryDirectory(prefix="msi-mux-package-", dir=build) as temp:
    bundle = Path(temp) / name
    payload = bundle / "payload"
    payload.mkdir(parents=True)
    subprocess.run(["cmake", "--install", str(build)], env={**os.environ, "DESTDIR": str(payload)}, check=True)
    actual = {str(p.relative_to(payload)) for p in payload.rglob("*") if p.is_file()}
    if actual != set(FILES):
        raise SystemExit(f"Unexpected CMake inventory. Missing: {set(FILES) - actual}; extra: {actual - set(FILES)}")
    checksums = []
    for relative in FILES:
        path = payload / relative
        if path.is_symlink():
            raise SystemExit(f"Package symlink rejected: {relative}")
        path.chmod(FILES[relative])
        checksums.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {relative}\n")
    (bundle / "SHA256SUMS").write_text("".join(checksums), encoding="ascii")
    for file in ("install.py", "uninstall.py", "managed_files.py"):
        shutil.copyfile(source / "packaging" / file, bundle / file)
    shutil.copyfile(source / "README.md", bundle / "README.md")
    subprocess.run([sys.executable, "-B", str(bundle / "install.py"), "--check"], check=True)
    # Numeric root ownership inside the archive, never derived from the builder.
    def normalize(info):
        info.uid = info.gid = 0
        info.uname = info.gname = "root"
        return info
    with tarfile.open(archive, "w:gz") as tar:
        tar.add(bundle, arcname=name, filter=normalize)
checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
(output / f"{archive.name}.sha256").write_text(f"{checksum}  {archive.name}\n", encoding="ascii")
print(archive)
