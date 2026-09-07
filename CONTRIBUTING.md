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

Add English and Turkish text together. Preserve keyboard navigation, readability in native themes and the optional reduced-motion preference. Current mode, local draft, firmware-pending target and observed internal display routing are distinct facts; animation must not imply that saving a draft changed firmware or completed a transition. Keep device details and release notes available through About.

In 0.5.0, Apply saves or replaces a local draft only. It must not invoke the helper, request authorization, change UEFI state or call MSI ACPI. Later sends neither firmware nor power requests. Selecting the current mode or explicitly canceling the draft clears it. Restore a valid draft when the application reopens in the same boot; invalidate it after a new boot without assuming it was applied. A restart or shutdown outside MSI MUX must never commit a local draft.

Only an explicit Restart/Power Off choice inside MSI MUX may commit the latest draft: fresh status and mode checks, restricted helper with authorization, verified success, then the selected normal desktop power request. Failure, cancellation or uncertainty before verified firmware success must suppress the power request. A desktop refusal after firmware success cannot undo the committed target. Preserve confirmation, unsaved-work inhibitors and cancellation; no forced request or fallback after an uncertain reply. Warm-restart effectiveness is not established by the existing hardware evidence.

Existing firmware-pending transactions are immutable until reconciled; local draft replacement or cancellation must not modify or erase them. Keep the pending journal and apply-ready checks. Launcher and local IPC mode requests may open the local-draft flow but cannot authorize a commit or power action. The CLI retains immediate firmware application after its mode-specific human-readable typed confirmation; it does not create desktop drafts.

GNOME panel integration uses the maintained AppIndicator/StatusNotifierItem adapter and the same Qt menu. Do not add another firmware parser, privileged controller or automatic extension installer. Tests with synthetic desktop names or a StatusNotifierItem host do not count as a native GNOME session test.

## Hardware support

Adding a model requires evidence for its exact layout, ACPI contract, physical routing, and failure handling. Similar names, GPUs, and UEFI variable names are insufficient. Never relax an identity check merely to enable another laptop.

Hardware tests require a deliberate plan and out-of-band recovery instructions. Record sanitized observations; distinguish actual mode changes from decoded capability bits. Separate owner-reported results from captured post-boot evidence, and record transition directions and boot methods only when supplied or observed.

## Releases

Version `0.5.0` offers the local-draft workflow for Hybrid, Discrete and Integrated on the exact model, board, and BIOS documented in `docs/VALIDATION.md`. Hybrid/Discrete have three captured post-boot observations; Integrated has the owner's explicit report of hardware success. Preserve the 0.4.0 software evidence and earlier 0.3.0 experimental-mode policy as history. The new desktop flow does not establish safe retargeting of an already acknowledged firmware transaction. New hardware or protocol changes require their own evidence before expanding support.

Failure handling is exercised through synthetic fault injection; intentionally failing real firmware is neither required nor appropriate for a stable release. Document recovery limits without promising universal recovery. Artifacts need checksums and installation/removal instructions. Never publish local diagnostic captures or inspected proprietary firmware binaries.
