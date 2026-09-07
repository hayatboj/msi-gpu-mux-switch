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

Add English and Turkish text together. Preserve keyboard navigation, readability in native themes and the optional reduced-motion preference. Current mode, requested mode, and observed internal display routing are distinct facts; pending animation must not imply physical completion. Keep device details and release notes available through About.

All three modes use the same typed confirmation and backend checks. Launcher and local IPC mode requests may open confirmation but must not authorize a write. Never automatically restart or power off a user. After a successful request, explicit Restart/Power Off choices use the active desktop's normal session handling, with Later available. Preserve confirmation, unsaved-work inhibitors and cancellation; no forced request or fallback after an uncertain reply. Warm-restart effectiveness is not established by the existing hardware evidence.

GNOME panel integration uses the maintained AppIndicator/StatusNotifierItem adapter and the same Qt menu. Do not add another firmware parser, privileged controller or automatic extension installer. Tests with synthetic desktop names or a StatusNotifierItem host do not count as a native GNOME session test.

## Hardware support

Adding a model requires evidence for its exact layout, ACPI contract, physical routing, and failure handling. Similar names, GPUs, and UEFI variable names are insufficient. Never relax an identity check merely to enable another laptop.

Hardware tests require a deliberate plan and out-of-band recovery instructions. Record sanitized observations; distinguish actual mode changes from decoded capability bits. Separate owner-reported results from captured post-boot evidence, and record transition directions and boot methods only when supplied or observed.

## Releases

Version `0.4.0` offers Hybrid, Discrete and Integrated on the exact model, board, and BIOS documented in `docs/VALIDATION.md`. Hybrid/Discrete have three captured post-boot observations; Integrated has the owner's explicit report of hardware success. The earlier `0.3.0` release kept Integrated experimental; retain that fact only in historical release records. New hardware or protocol changes require their own evidence before expanding support.

Failure handling is exercised through synthetic fault injection; intentionally failing real firmware is neither required nor appropriate for a stable release. Document recovery limits without promising universal recovery. Artifacts need checksums and installation/removal instructions. Never publish local diagnostic captures or inspected proprietary firmware binaries.
