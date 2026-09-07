# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.4.0] - 2026-09-07

### Added

- Animated graphics-mode hero: red Discrete, amber Hybrid, teal Integrated;
  pending mode changes display both current and requested modes immediately.
- Directional current-to-target gradients and mode illustrations, including a
  rocket for Discrete, with reduced-motion controls and hidden-window suspension.
- About dialog with hayatboj's GitHub profile, project links, credits, embedded
  release notes in English/Turkish, and a user-copyable system summary.
- Explicit Restart, Power off and Later choices after a successful mode request,
  using the active KDE/GNOME session manager and retaining desktop cancellation.
- GNOME tray setup guidance through maintained AppIndicator support and desktop
  launcher actions that open the existing guarded mode-confirmation dialog.
- Synthetic mode/target previews for testing each pending transition without
  firmware or power operations.

### Changed

- Integrated is now available as an ordinary mode after the device owner reported
  a successful real test. The exact model/board/BIOS and capability gates remain.
- Moved hardware validation information from the main window to About.
- Current physical mode remains visible until a new boot verifies activation;
  a requested mode never masquerades as an already active GPU connection.
- Power controls remain available for a valid pending transaction after reopening
  the app. No automatic shutdown, countdown, forced session exit or power action.

### Validation limits

- The owner's Integrated result is distinct from the three recorded Hybrid/Discrete
  post-boot captures. No additional firmware transition was run for this UI release.
- A normal reboot's MUX activation is not established by those full-power-cycle tests.
  The power dialog retains the complete shutdown option and explains that limit.
- GNOME uses the existing StatusNotifierItem protocol through AppIndicator; a
  native GNOME session was not available on the reference KDE host.

## [0.3.0] - 2026-09-07

### Added

- Stable Linux desktop support for Hybrid and Discrete switching on the exact
  MSI Vector 16 HX AI A2XWIG / MS-15M3 / E15M3IMS.116 configuration.
- Recorded three successful live transitions using installed `0.3.0-rc.1`,
  commit `fda56b8905e988a73d3555b734ada4a38179f64a`: Hybrid to Discrete
  verified at 15:19:10, Discrete to Hybrid at 15:30:03, and Hybrid to Discrete
  again at 15:36:52 on 2026-09-07 (UTC+03:00). Every transition followed a
  manual full shutdown/power-on. Current/target firmware modes and the active
  internal panel route agreed, preserving 2560×1600 at 240 Hz.

### Changed

- Integrated mode is experimental and disabled by default in the desktop;
  users must explicitly enable experimental modes in settings. CLI Integrated
  support and the backend's original confirmation and safety gates remain intact.
- Stable release scope distinguishes validated default Hybrid/Discrete operation
  from unvalidated Integrated operation and unproven hardware recovery.
- Firmware protocol and transaction engine are unchanged from the hardware-tested
  release candidate; failure handling remains covered by synthetic tests.

### Fixed

- Tray icon registration follows KDE system tray availability, including a tray
  that becomes available after application startup.

## [0.3.0-rc.1] - 2026-09-07

### Added

- Native Qt 6 KDE tray application with current and requested mode, internal
  panel GPU, right-click switching, English/Turkish translation, and opt-in autostart.
- Restricted root helper and Polkit authentication; the desktop stays unprivileged.
- Read-only `--status --json` polling without ACPI calls.
- Durable transaction journal, interprocess locking, interrupted-operation
  detection, and explicit handling of uncertain firmware command completion.
- Linux installation bundle with checksums, validated installation/removal,
  Arch packaging, desktop metadata, and software validation in CI.
- Synthetic transaction, GUI, helper, and installer tests; recovery guidance
  and an explicit hardware validation checklist.

### Changed

- Writes now fail closed outside the characterized model, board, and BIOS.
- Current and requested modes remain distinct until a full shutdown/power-on.
- Linux releases are explicitly experimental pending real MUX/recovery validation.
- Windows CLI support and upstream protocol definitions are preserved.

## [0.2.0] - 2026-08-31

### Added

- Linux diagnostics and mode switching using DMI, efivarfs, PCI sysfs, and the
  kernel `msi-wmi-platform` debugfs interface.
- Linux AMD64 CI coverage and release archives with checksums.

### Changed

- Linux switching preserves the efivarfs inode flags around exact firmware
  writes and retains the same hardware, BIOS, power, confirmation, preflight,
  verification, rollback, and manual-shutdown protections as Windows.
- Platform-specific system access now implements shared platform and ACPI
  traits in separate Windows and Linux modules.
- Reworked the README with cross-platform installation, usage, prerequisites,
  safety, rollback, JSON, and development guidance.

## [0.1.0] - 2026-08-31

### Added

- Windows CLI for selecting MSHybrid, discrete, or integrated GPU MUX modes on
  the MSI Vector 16 HX AI A2XWIG with the MS-15M3 motherboard.
- Interactive mode selection and positional mode aliases.
- Structured JSON diagnostics and newline-delimited JSON switching events.
- Exact hardware and BIOS gates, Administrator and AC-power checks, firmware
  layout validation, write verification, rollback metadata, and automatic
  target restoration on apply failures.
- MSI ACPI apply handshake with readiness polling and acknowledgement.
- Embedded Windows manifest requesting Administrator privileges.
- Windows CI, release archives, SHA-256 checksums, and tagged GitHub Releases.

[Unreleased]: https://github.com/hayatboj/msi-gpu-mux-switch/compare/v0.4.0...linux-kde
[0.4.0]: https://github.com/hayatboj/msi-gpu-mux-switch/releases/tag/v0.4.0
[0.3.0]: https://github.com/hayatboj/msi-gpu-mux-switch/releases/tag/v0.3.0
[0.3.0-rc.1]: https://github.com/hayatboj/msi-gpu-mux-switch/releases/tag/v0.3.0-rc.1
[0.2.0]: https://github.com/steelbrain/msi-gpu-mux-switch/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/steelbrain/msi-gpu-mux-switch/releases/tag/v0.1.0
