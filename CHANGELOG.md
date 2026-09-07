# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- Documented a successful live Linux MSHybrid to Discrete and back to MSHybrid round trip on the
  MSI Vector 16 HX AI A2XWIG / MS-15M3 / E15M3IMS.116, using installed
  `0.3.0-rc.1` at commit `fda56b8905e988a73d3555b734ada4a38179f64a`.
  Privileged preflight showed apply-ready clear; the user approved the test,
  the request was acknowledged, and the internal eDP panel was verified as
  NVIDIA-driven after a manual full shutdown and power-on on 2026-09-07.
  The user-approved reverse test then verified Hybrid firmware state and an
  Intel-driven internal eDP panel after another manual full shutdown and power-on.
  Both directions retained 2560×1600 at 240 Hz, using the same installed version.
  Integrated mode and live failure recovery remain unvalidated;
  the release remains a release candidate.

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

[Unreleased]: https://github.com/hayatboj/msi-gpu-mux-switch/compare/v0.3.0-rc.1...linux-kde
[0.3.0-rc.1]: https://github.com/hayatboj/msi-gpu-mux-switch/releases/tag/v0.3.0-rc.1
[0.2.0]: https://github.com/steelbrain/msi-gpu-mux-switch/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/steelbrain/msi-gpu-mux-switch/releases/tag/v0.1.0
