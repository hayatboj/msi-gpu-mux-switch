#!/usr/bin/env python3
"""Synthetic package validation: no system installation or firmware access."""
import hashlib
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import install as installer
from install import load_package, trusted_parents
from managed_files import FILES
from uninstall import uninstall


class PackageValidation(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        lines = []
        for name in FILES:
            path = self.root / "payload" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            data = ("synthetic package fixture: " + name).encode()
            path.write_bytes(data)
            lines.append(f"{hashlib.sha256(data).hexdigest()}  {name}\n")
        self.manifest = self.root / "SHA256SUMS"
        self.manifest.write_text("".join(lines), encoding="ascii")

    def tearDown(self):
        self.temporary.cleanup()

    def test_complete_package(self):
        content, checksums = load_package(self.root)
        self.assertEqual(set(content), set(FILES))
        self.assertEqual(set(checksums), set(FILES))

    def test_payload_corruption(self):
        (self.root / "payload/usr/bin/msi-mux-switch").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            load_package(self.root)

    def test_extra_manifest_path(self):
        with self.manifest.open("a") as output:
            output.write("0" * 64 + "  ../../etc/shadow\n")
        with self.assertRaisesRegex(ValueError, "Invalid package checksum"):
            load_package(self.root)

    def test_duplicate_manifest_path(self):
        with self.manifest.open("a") as output:
            output.write(self.manifest.read_text().splitlines()[0] + "\n")
        with self.assertRaisesRegex(ValueError, "Invalid package checksum"):
            load_package(self.root)

    def test_missing_manifest_path(self):
        self.manifest.write_text("\n".join(self.manifest.read_text().splitlines()[1:]) + "\n")
        with self.assertRaisesRegex(ValueError, "inventory"):
            load_package(self.root)

    def test_payload_symlink(self):
        target = self.root / "payload/usr/bin/msi-mux-switch"
        other = target.with_name("other")
        target.rename(other)
        target.symlink_to(other)
        with self.assertRaises(OSError):
            load_package(self.root)

    def test_payload_symlink_directory(self):
        target = self.root / "payload/usr/bin"
        other = target.with_name("other-bin")
        target.rename(other)
        target.symlink_to(other, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "Invalid package directory"):
            load_package(self.root)

    def test_payload_hardlink(self):
        target = self.root / "payload/usr/bin/msi-mux-switch"
        os.link(target, self.root / "extra-link")
        with self.assertRaisesRegex(ValueError, "extra links"):
            load_package(self.root)

    def test_fifo_cannot_block_validation(self):
        target = self.root / "payload/usr/bin/msi-mux-switch"
        target.unlink()
        os.mkfifo(target)
        with self.assertRaisesRegex(ValueError, "regular file"):
            load_package(self.root)

    def test_writable_destination_directory_rejected(self):
        self.root.chmod(0o777)
        with self.assertRaisesRegex(ValueError, "Untrusted installation directory"):
            trusted_parents(self.root / "usr/bin/test", self.root)


class SimulatedInstall(unittest.TestCase):
    """Real file operations confined to a temporary tree; simulated root uid."""
    def setUp(self):
        PackageValidation.setUp(self)
        self.destination = self.root / "simulated-system"
        self.destination.mkdir()
        original_lstat = Path.lstat

        def root_owned_lstat(path, *args, **kwargs):
            result = original_lstat(path, *args, **kwargs)
            values = list(result)
            values[4] = 0
            return os.stat_result(values)

        self.patches = [mock.patch("os.geteuid", return_value=0),
                        mock.patch("os.fchown"), mock.patch("builtins.print"),
                        mock.patch.object(Path, "lstat", root_owned_lstat)]
        for patch in self.patches:
            patch.start()

    def tearDown(self):
        for patch in reversed(self.patches):
            patch.stop()
        PackageValidation.tearDown(self)

    def test_install_update_uninstall_in_temporary_tree(self):
        installer.install(self.root, self.destination)
        for name, mode in FILES.items():
            path = self.destination / name
            self.assertEqual(path.read_bytes(), (self.root / "payload" / name).read_bytes())
            self.assertEqual(path.stat().st_mode & 0o777, mode)
        installer.install(self.root, self.destination)
        uninstall(self.destination)
        self.assertTrue(all(not (self.destination / name).exists() for name in FILES))

    def test_modified_file_blocks_uninstall_and_update(self):
        installer.install(self.root, self.destination)
        (self.destination / "usr/bin/msi-mux-switch").write_bytes(b"user modification")
        with self.assertRaisesRegex(ValueError, "modified file"):
            uninstall(self.destination)
        with self.assertRaisesRegex(ValueError, "modified file"):
            installer.install(self.root, self.destination)
        self.assertTrue((self.destination / "usr/bin/msi-mux-tray").exists())

    def test_unmanaged_file_blocks_first_install(self):
        target = self.destination / "usr/bin/msi-mux-switch"
        target.parent.mkdir(parents=True)
        target.write_bytes(b"another installation")
        with self.assertRaisesRegex(ValueError, "unmanaged"):
            installer.install(self.root, self.destination)
        self.assertEqual(target.read_bytes(), b"another installation")

    def test_mid_install_failure_rolls_back_managed_files(self):
        original_write = installer.atomic_write

        def fail_second_file(path, data, mode):
            if path.name == "msi-mux-tray":
                raise OSError("synthetic write failure")
            original_write(path, data, mode)

        with mock.patch.object(installer, "atomic_write", fail_second_file):
            with self.assertRaisesRegex(OSError, "synthetic write failure"):
                installer.install(self.root, self.destination)
        self.assertTrue(all(not (self.destination / name).exists() for name in FILES))


if __name__ == "__main__":
    unittest.main()
