# Validation record

Software tests and live hardware tests are separate. Record observed results; a passing build does not establish physical MUX switching.

## Reference environment

MSI Vector 16 HX AI A2XWIG / MS-15M3; BIOS E15M3IMS.116, EC 15M3EMS1.113. Intel Core Ultra 9 275HX + RTX 5080 Laptop. CachyOS kernel 7.2.2, KDE Plasma 6.7.4 Wayland, Qt 6.11.2, NVIDIA 610.57.04.

No serial numbers, raw firmware, account identifiers, or encryption material belong here.

## Initial read-only observations

- Model, motherboard, BIOS and MSI WMI binding match.
- OEM variable has the expected 20-byte payload and attributes `0x00000007`.
- Requested and current modes decode as MSHybrid; Discrete/Integrated capability bits are present.
- Starting internal eDP route is Intel.

These observations describe the starting state before the live test below. Read-only diagnostics alone do not validate a write.

## Software release gate

- [x] Rust formatting, strict Clippy on Linux/Windows targets, and 22 Rust tests pass.
- [x] Failure tests cover ambiguous ACPI completion, durable phases, stale state, and journal recovery classification.
- [x] Qt compilation, parsing, mode gating, localization and safe process lifetime pass: 40 Qt test results, including initialization/cleanup.
- [x] English and Turkish screenshots visually checked at 680×900; no clipped controls.
- [x] Real KDE StatusNotifierItem registration verified in inert demo mode; menu labels, active mode, language switching and clean exit verified over D-Bus.
- [x] Helper restrictions (12 cases) and staged installation/removal tests (14 cases) pass; desktop/AppStream metadata validates.
- [x] Linux and native Windows CI pass on implementation commit `3109232`; [recorded CI run](https://github.com/hayatboj/msi-gpu-mux-switch/actions/runs/34119405294).
- [x] Linux/Windows bundles build from that commit; the Linux archive checksum and complete 16-file payload validate. Both GitHub-built Linux executables launch on CachyOS; [build run](https://github.com/hayatboj/msi-gpu-mux-switch/actions/runs/34119405288).

Local software checks above completed on 2026-09-07. See [CI](https://github.com/hayatboj/msi-gpu-mux-switch/actions/workflows/ci.yml) for the exact commit's remote Linux/Windows results. Demo tests never invoked the privileged helper. The helper has PIE, full RELRO and a non-executable stack. Installation tests used an isolated synthetic system tree.

Release packaging reruns on the release tag. Check its [release build](https://github.com/hayatboj/msi-gpu-mux-switch/actions/workflows/release.yml) and downloaded `SHA256SUMS` when installing; the results above identify the tested implementation, not arbitrary future commits. GitHub Linux archives build on Ubuntu 24.04 with system Qt 6 and do not bundle Qt.

## Live Linux hardware observation

One **MSHybrid → Discrete** transition was observed successfully on the reference laptop on **2026-09-07**. The installed application was **0.3.0-rc.1**, commit [`fda56b8905e988a73d3555b734ada4a38179f64a`](https://github.com/hayatboj/msi-gpu-mux-switch/commit/fda56b8905e988a73d3555b734ada4a38179f64a), with model **Vector 16 HX AI A2XWIG**, board **MS-15M3**, and BIOS **E15M3IMS.116**.

1. Privileged diagnostics on the preceding boot succeeded and showed apply-ready clear.
2. The user explicitly approved the live Hybrid to Discrete test.
3. The Discrete request completed successfully, including the firmware acknowledgement.
4. The user performed a manual full shutdown and power-on.
5. Post-boot read-only status from the installed backend, captured at **2026-09-07T15:19:10+03:00**, reported both current mode and selected target as `discrete`, with `pending_shutdown=false` and `switching_supported=true`.
6. The active internal connector **`card1-eDP-1`** was driven by **NVIDIA**, using the `nvidia` driver at **PCI `0000:01:00.0`**. PCI display enumeration showed only NVIDIA. KDE retained the native **2560×1600 at 240 Hz** display mode. This confirms the physical internal display route after the power cycle, beyond merely selecting a target mode.

This record summarizes the observations without publishing raw firmware or diagnostic captures. **Return to Hybrid, Integrated mode, and live failure recovery remain unvalidated.** One successful transition does not establish compatibility with other BIOS versions or laptops, or guarantee recovery from a failed operation.

## Hardware release gate

- [x] Privileged diagnostics succeed with apply-ready clear.
- [x] User explicitly approves the live Hybrid to Discrete test.
- [x] Hybrid to Discrete request succeeds on the reference laptop, including acknowledgement.
- [x] Complete manual shutdown and power-on performed.
- [x] Firmware reports Discrete and active internal eDP is NVIDIA-driven.
- [ ] Return to Hybrid verified after another full shutdown/power-on.
- [ ] Integrated mode separately validated; it remains unvalidated.
- [x] Tested application commit and BIOS/EC versions recorded.

The release remains a prerelease with the tested direction and outstanding validation limits stated explicitly. Never intentionally cause a real firmware failure to test recovery.
