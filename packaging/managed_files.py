"""Explicit package inventory shared by installer and packager."""
FILES = {
    "usr/bin/msi-mux-switch": 0o755,
    "usr/bin/msi-mux-tray": 0o755,
    "usr/lib/msi-mux/msi-mux-helper": 0o755,
    "usr/share/polkit-1/actions/org.hayatboj.msimux.policy": 0o644,
    "usr/share/applications/org.hayatboj.msimux.desktop": 0o644,
    "usr/share/metainfo/org.hayatboj.msimux.metainfo.xml": 0o644,
    "usr/share/icons/hicolor/scalable/apps/org.hayatboj.msimux.svg": 0o644,
    "usr/share/doc/msi-mux/README.md": 0o644,
    "usr/share/doc/msi-mux/README.tr.md": 0o644,
    "usr/share/doc/msi-mux/CHANGELOG.md": 0o644,
    "usr/share/doc/msi-mux/LICENSE": 0o644,
    "usr/share/doc/msi-mux/SECURITY.md": 0o644,
    "usr/share/doc/msi-mux/docs/RECOVERY.md": 0o644,
    "usr/share/doc/msi-mux/docs/VALIDATION.md": 0o644,
    "usr/share/doc/msi-mux/docs/images/kde-en.png": 0o644,
    "usr/share/doc/msi-mux/docs/images/kde-tr.png": 0o644,
}
RECORD = "var/lib/msi-mux/installed.json"
