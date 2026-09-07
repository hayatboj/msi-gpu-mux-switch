#!/usr/bin/env python3
"""Exercise only argument rejection and inert previews in an isolated Qt session."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = str(Path(sys.argv.pop(1)).resolve())


class LaunchTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="msi-mux-preview-test-")
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.env = dict(os.environ, QT_QPA_PLATFORM="offscreen", XDG_CONFIG_HOME=str(root / "config"),
                        XDG_RUNTIME_DIR=str(root), XDG_DATA_HOME=str(root / "data"), DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent",
                        XDG_CURRENT_DESKTOP="")
        root.chmod(0o700)

    def run_app(self, *arguments):
        return subprocess.run([BINARY, *arguments], env=self.env, cwd=self.directory.name, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)

    def test_rejects_invalid_and_non_demo_preview_options(self):
        for arguments in [("--request-mode", "invalid"), ("--demo-mode", "discrete"),
                          ("--demo-target", "integrated"), ("--demo-selection", "integrated"), ("--screenshot", "/nonexistent/out.png"),
                          ("--screenshot-page", "about"),
                          ("--demo", "--demo-mode", "invalid"),
                          ("--demo", "--demo-selection", "invalid"),
                          ("--demo", "--demo-target", "discrete", "--demo-selection", "integrated", "--screenshot", "unused.png"),
                          ("--demo", "--screenshot", "unused.png", "--request-mode", "discrete")]:
            with self.subTest(arguments=arguments):
                self.assertEqual(self.run_app(*arguments).returncode, 2)

    def test_bilingual_pending_and_about_previews_are_inert(self):
        for language in ("en", "tr"):
            for page in ("main", "about", "changes"):
                with self.subTest(language=language, page=page):
                    path = Path(self.directory.name) / f"{language}-{page}.png"
                    result = self.run_app("--demo", "--language", language, "--demo-mode", "discrete",
                                          "--demo-target", "mshybrid", "--screenshot", str(path),
                                          "--screenshot-page", page)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertTrue(path.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))
                    self.assertGreater(path.stat().st_size, 10000)
        self.assertFalse((Path(self.directory.name) / "config/hayatboj/MSI MUX.conf").exists())

    def test_local_selection_preview_does_not_persist_or_apply(self):
        for language in ("en", "tr"):
            with self.subTest(language=language):
                path = Path(self.directory.name) / f"{language}-selection.png"
                result = self.run_app("--demo", "--language", language, "--demo-selection", "integrated",
                                      "--screenshot", str(path))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(path.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))
        self.assertFalse(list(Path(self.directory.name).rglob("selection.json")))


if __name__ == "__main__":
    unittest.main()
