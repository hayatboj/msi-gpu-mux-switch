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
from managed_files import FILES, RECORD
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

    def prepare_upgrade(self):
        """Different release payloads and a synthetic journal, all in the temp tree."""
        def package_version(version):
            lines = []
            for name in FILES:
                data = f"synthetic {version} fixture: {name}".encode()
                (self.root / "payload" / name).write_bytes(data)
                lines.append(f"{hashlib.sha256(data).hexdigest()}  {name}\n")
            self.manifest.write_text("".join(lines), encoding="ascii")

        package_version("0.3.0-rc.1")
        installer.install(self.root, self.destination)
        originals = {name: (self.destination / name).read_bytes() for name in FILES}
        record = (self.destination / RECORD).read_bytes()
        journal = self.destination / "var/lib/msi-mux/transaction.json"
        journal.write_bytes(b'{"synthetic_test_fixture":true}\n')
        journal.chmod(0o600)
        package_version("0.3.0")
        return originals, record, journal, journal.read_bytes()

    def test_rc1_to_stable_replaces_files_atomically_and_retains_journal(self):
        originals, _, journal, journal_data = self.prepare_upgrade()
        old_inodes = {name: (self.destination / name).stat().st_ino for name in FILES}
        installer.install(self.root, self.destination)
        _, expected_hashes = load_package(self.root)
        self.assertEqual(installer.read_installed(self.destination)["files"], expected_hashes)
        for name, mode in FILES.items():
            path = self.destination / name
            self.assertNotEqual(path.read_bytes(), originals[name])
            self.assertEqual(path.read_bytes(), (self.root / "payload" / name).read_bytes())
            self.assertNotEqual(path.stat().st_ino, old_inodes[name])
            self.assertEqual(path.stat().st_mode & 0o777, mode)
        self.assertEqual(journal.read_bytes(), journal_data)
        self.assertEqual(journal.stat().st_mode & 0o777, 0o600)
        uninstall(self.destination)
        self.assertEqual(journal.read_bytes(), journal_data)

    def test_modified_rc1_file_blocks_stable_upgrade_before_replacement(self):
        originals, record, journal, journal_data = self.prepare_upgrade()
        # A late inventory entry ensures validation finishes before any replacement.
        name = list(FILES)[-1]
        changed = self.destination / name
        changed.write_bytes(b"local modification")
        with mock.patch.object(installer, "atomic_write") as write:
            with self.assertRaisesRegex(ValueError, "modified file"):
                installer.install(self.root, self.destination)
            write.assert_not_called()
        for original_name, data in originals.items():
            expected = b"local modification" if original_name == name else data
            self.assertEqual((self.destination / original_name).read_bytes(), expected)
        self.assertEqual((self.destination / RECORD).read_bytes(), record)
        self.assertEqual(journal.read_bytes(), journal_data)

    def test_failed_stable_upgrade_restores_rc1_and_retains_journal(self):
        originals, record, journal, journal_data = self.prepare_upgrade()
        original_write = installer.atomic_write
        failed = False

        def fail_once(path, data, mode):
            nonlocal failed
            if path.name == "msi-mux-helper" and not failed:
                failed = True
                raise OSError("synthetic upgrade failure")
            original_write(path, data, mode)

        with mock.patch.object(installer, "atomic_write", fail_once):
            with self.assertRaisesRegex(OSError, "synthetic upgrade failure"):
                installer.install(self.root, self.destination)
        for name, data in originals.items():
            self.assertEqual((self.destination / name).read_bytes(), data)
        self.assertEqual((self.destination / RECORD).read_bytes(), record)
        self.assertEqual(journal.read_bytes(), journal_data)

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
