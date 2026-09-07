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

Add English and Turkish text together. Preserve keyboard navigation and readability in native themes. Current mode, requested mode, and observed internal display routing are distinct facts. Do not label a requested transition as physically complete before post-boot observation. Never automatically reboot a user.

## Hardware support

Adding a model requires evidence for its exact layout, ACPI contract, physical routing, and failure handling. Similar names, GPUs, and UEFI variable names are insufficient. Never relax an identity check merely to enable another laptop.

Hardware tests require a deliberate plan and out-of-band recovery instructions. Record sanitized observations; distinguish actual mode changes from decoded capability bits.

## Releases

Use prerelease tags until `docs/VALIDATION.md` records the hardware gate as complete. Artifacts need checksums and installation/removal instructions. Never publish local diagnostic captures or inspected proprietary firmware binaries.
