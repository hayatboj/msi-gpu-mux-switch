#!/usr/bin/env python3
"""Remove only unchanged files recorded by this bundle's installer.

Per-user preferences and firmware backup metadata are preserved.
"""
import os
from pathlib import Path
import sys

sys.dont_write_bytecode = True
from install import read_installed, verify_installed
from managed_files import FILES, RECORD


def uninstall(root=Path("/")):
    if os.geteuid() != 0:
        raise ValueError("Run this uninstaller as root")
    saved = read_installed(root)
    if saved is None:
        raise ValueError("No bundle installation record found; use your package manager if installed from a package")
    verify_installed(root, saved)
    for name in FILES:
        (root / name).unlink()
    (root / RECORD).unlink()
    print("Removed MSI MUX managed files. User preferences and firmware backups were preserved.")


if __name__ == "__main__":
    try:
        if len(sys.argv) != 1:
            raise ValueError("This uninstaller accepts no arguments")
        uninstall()
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Uninstallation failed: {error}", file=sys.stderr)
        sys.exit(1)
