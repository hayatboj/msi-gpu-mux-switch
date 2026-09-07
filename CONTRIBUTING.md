# Contributing

Read [AGENTS.md](AGENTS.md) before editing the protocol and [SECURITY.md](SECURITY.md) before changing privilege boundaries.

## Layout and checks

- `src/`: Rust diagnostics, firmware transactions, and platform integration.
- `gui/`: Qt desktop, tray, translations, and GUI tests.
- `packaging/`: privileged helper, desktop metadata, Polkit, distro packaging.
- `scripts/`: build, release staging, installation/removal.
- `docs/`: recovery and hardware validation.

```sh
cargo fmt --all --check
cargo clippy --locked --all-targets -- -D warnings
cargo test --locked
./scripts/build-linux.sh
```

Run GUI tests with the offscreen Qt platform and use demo mode for screenshots. Demo/test modes must never request administrator access or perform real transitions. Production helper paths remain fixed.

## Interface

Add English and Turkish text together. Preserve keyboard navigation and readability in native themes. Current mode, requested mode, and observed internal display routing are distinct facts. Do not label a requested transition as physically complete before post-boot observation. Never automatically reboot a user. Integrated mode stays disabled by default behind an explicit desktop experimental-mode preference; enabling that preference must not weaken backend checks.

## Hardware support

Adding a model requires evidence for its exact layout, ACPI contract, physical routing, and failure handling. Similar names, GPUs, and UEFI variable names are insufficient. Never relax an identity check merely to enable another laptop.

Hardware tests require a deliberate plan and out-of-band recovery instructions. Record sanitized observations; distinguish actual mode changes from decoded capability bits.

## Releases

Version `0.3.0` is stable for Hybrid/Discrete switching on the exact model, board, and BIOS documented in `docs/VALIDATION.md`. Integrated remains outside that validated scope and requires explicit desktop opt-in. New hardware or protocol changes require their own evidence before expanding stable support.

Failure handling is exercised through synthetic fault injection; intentionally failing real firmware is neither required nor appropriate for a stable release. Document recovery limits without promising universal recovery. Artifacts need checksums and installation/removal instructions. Never publish local diagnostic captures or inspected proprietary firmware binaries.
