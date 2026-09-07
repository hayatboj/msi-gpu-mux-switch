# Validation record

Software tests and live hardware tests are separate. Record observed results; a passing build does not establish physical MUX switching.

## Reference environment

MSI Vector 16 HX AI A2XWIG / MS-15M3; BIOS E15M3IMS.116, EC 15M3EMS1.113. Intel Core Ultra 9 275HX + RTX 5080 Laptop. CachyOS kernel 7.2.2, KDE Plasma 6.7.4 Wayland, Qt 6.11.2, NVIDIA 610.57.04.

No serial numbers, raw firmware, account identifiers, or encryption material belong here.

## Read-only observations

- Model, motherboard, BIOS and MSI WMI binding match.
- OEM variable has the expected 20-byte payload and attributes `0x00000007`.
- Requested and current modes decode as MSHybrid; Discrete/Integrated capability bits are present.
- Starting internal eDP route is Intel.

These observations do not validate a write.

## Software release gate

- [x] Rust formatting, strict Clippy on Linux/Windows targets, and 22 Rust tests pass.
- [x] Failure tests cover ambiguous ACPI completion, durable phases, stale state, and journal recovery classification.
- [x] Qt compilation, parsing, mode gating, localization and safe process lifetime pass: 40 Qt test results, including initialization/cleanup.
- [x] English and Turkish screenshots visually checked at 680×900; no clipped controls.
- [x] Real KDE StatusNotifierItem registration verified in inert demo mode; menu labels, active mode, language switching and clean exit verified over D-Bus.
- [x] Helper restrictions (12 cases) and staged installation/removal tests (14 cases) pass; desktop/AppStream metadata validates.
- [ ] Linux and Windows CI pass on the published commit.
- [ ] Release artifacts/checksums match the tested commit.

Local software checks above completed on 2026-09-07. See [CI](https://github.com/hayatboj/msi-gpu-mux-switch/actions/workflows/ci.yml) for the exact commit's remote Linux/Windows results. Demo tests never invoked the privileged helper. The helper has PIE, full RELRO and a non-executable stack. Installation tests used an isolated synthetic system tree.

## Hardware release gate

- [ ] Privileged diagnostics succeed with apply-ready clear.
- [ ] Actual operation and recovery limits reviewed by the user.
- [ ] Hybrid to Discrete request succeeds on the reference laptop.
- [ ] Complete shutdown and power-on performed.
- [ ] Firmware reports Discrete and active internal eDP is NVIDIA-driven.
- [ ] Return to Hybrid verified after another full shutdown/power-on.
- [ ] Integrated mode separately validated or explicitly remains unvalidated.
- [ ] Tested application commit and BIOS/EC versions recorded.

Until hardware validation is complete, publish a prerelease and label Linux switching experimental. Never intentionally cause a real firmware failure to test recovery.
