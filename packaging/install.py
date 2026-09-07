#!/usr/bin/env python3
"""Offline installer. Run --check without privileges before installing.

Checksums detect damaged packages; authenticate the release independently.
Only explicit managed paths are installed. No firmware access is performed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import stat
import sys
import tempfile

sys.dont_write_bytecode = True
from managed_files import FILES, RECORD


def digest(data):
    return hashlib.sha256(data).hexdigest()


def regular_bytes(path):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, "rb") as source:
        st = os.fstat(source.fileno())
        if not stat.S_ISREG(st.st_mode) or st.st_nlink != 1:
            raise ValueError(f"Expected a regular file without extra links: {path}")
        return source.read()


def no_symlink_ancestors(path, root):
    relative = path.relative_to(root)
    cursor = root
    if root.is_symlink():
        raise ValueError(f"Symlink package directory: {root}")
    for part in relative.parts[:-1]:
        cursor /= part
        if cursor.is_symlink() or not cursor.is_dir():
            raise ValueError(f"Invalid package directory: {cursor}")


def load_package(bundle):
    checksums = {}
    for line in regular_bytes(bundle / "SHA256SUMS").decode("ascii").splitlines():
        checksum, separator, name = line.partition("  ")
        if (not separator or name not in FILES or name in checksums or
                len(checksum) != 64 or any(c not in "0123456789abcdef" for c in checksum)):
            raise ValueError("Invalid package checksum manifest")
        checksums[name] = checksum
    if checksums.keys() != FILES.keys():
        raise ValueError("Package file inventory does not match this installer")
    payload = bundle / "payload"
    content = {}
    for name in FILES:
        path = payload / name
        no_symlink_ancestors(path, bundle)
        data = regular_bytes(path)
        if digest(data) != checksums[name]:
            raise ValueError(f"Checksum mismatch: {name}")
        content[name] = data
    return content, checksums


def trusted_parents(path, root, create=False):
    directories = [root]
    for component in path.relative_to(root).parts[:-1]:
        directories.append(directories[-1] / component)
    for cursor in directories:
        try:
            st = cursor.lstat()
        except FileNotFoundError:
            if not create:
                continue
            cursor.mkdir(mode=0o755)
            st = cursor.lstat()
        if not stat.S_ISDIR(st.st_mode) or st.st_uid != 0 or st.st_mode & 0o022:
            raise ValueError(f"Untrusted installation directory: {cursor}")


def read_installed(root):
    record = root / RECORD
    trusted_parents(record, root)
    try:
        data = regular_bytes(record)
    except FileNotFoundError:
        return None
    st = record.lstat()
    if st.st_uid != 0 or st.st_mode & 0o022:
        raise ValueError("Installation record is not trusted")
    saved = json.loads(data)
    if (saved.get("schema_version") != 1 or
            set(saved.get("files", {})) != set(FILES)):
        raise ValueError("Invalid installation record")
    for value in saved["files"].values():
        if not isinstance(value, str) or len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
            raise ValueError("Invalid installation checksum")
    return saved


def verify_installed(root, saved):
    for name in FILES:
        path = root / name
        trusted_parents(path, root)
        try:
            data = regular_bytes(path)
        except FileNotFoundError:
            if saved:
                raise ValueError(f"Managed file is missing; refusing partial update: {path}") from None
            continue
        st = path.lstat()
        if st.st_uid != 0 or st.st_mode & 0o022:
            raise ValueError(f"Installed file is not trusted: {path}")
        if not saved or digest(data) != saved["files"][name]:
            raise ValueError(f"Refusing to overwrite/remove an unmanaged or modified file: {path}")


def atomic_write(path, data, mode):
    descriptor, temporary = tempfile.mkstemp(prefix=".msi-mux-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as output:
            os.fchmod(output.fileno(), mode)
            os.fchown(output.fileno(), 0, 0)
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def install(bundle, root=Path("/")):
    if os.geteuid() != 0:
        raise ValueError("Run --check as your user, then run this installer as root")
    if sys.platform != "linux" or platform.machine() != "x86_64":
        raise ValueError("This bundle supports Linux x86_64 only")
    content, checksums = load_package(bundle)
    saved = read_installed(root)
    verify_installed(root, saved)
    record = root / RECORD
    # Save existing contents before replacing anything; restore on failure.
    originals = {name: regular_bytes(root / name) for name in FILES} if saved else {}
    previous_record = regular_bytes(record) if saved else None
    completed = []
    try:
        for name, mode in FILES.items():
            path = root / name
            trusted_parents(path, root, create=True)
            atomic_write(path, content[name], mode)
            completed.append(name)
        trusted_parents(record, root, create=True)
        atomic_write(record, json.dumps({"schema_version": 1, "files": checksums}, indent=2).encode() + b"\n", 0o600)
    except Exception:
        for name in reversed(completed):
            if name in originals:
                atomic_write(root / name, originals[name], FILES[name])
            else:
                (root / name).unlink()
        if previous_record is not None:
            atomic_write(record, previous_record, 0o600)
        raise
    print("Installed MSI MUX. Launch msi-mux-tray from your desktop as your normal user.")
    print("Autostart remains opt-in. No firmware operation or restart was performed.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify the complete payload without installing or requiring root")
    args = parser.parse_args()
    bundle = Path(__file__).resolve().parent
    if args.check:
        content, _ = load_package(bundle)
        print(f"Verified {len(content)} payload files. No installation or firmware access performed.")
    else:
        install(bundle)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(f"Installation failed: {error}", file=sys.stderr)
        sys.exit(1)
