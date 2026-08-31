# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Linux diagnostics and mode switching using DMI, efivarfs, PCI sysfs, and the
  kernel `msi-wmi-platform` debugfs interface.
- Linux AMD64 release archives with checksums.

### Changed

- Linux switching preserves the efivarfs inode flags around exact firmware
  writes and retains the same hardware, BIOS, power, confirmation, preflight,
  verification, rollback, and manual-shutdown protections as Windows.

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

[Unreleased]: https://github.com/steelbrain/msi-gpu-mux-switch/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/steelbrain/msi-gpu-mux-switch/releases/tag/v0.1.0
