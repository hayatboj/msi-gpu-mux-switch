# Validation record

**Version 0.4.0 scope:** Hybrid, Discrete and Integrated are ordinary mode choices on MSI Vector 16 HX AI A2XWIG / MS-15M3 / E15M3IMS.116. Hybrid/Discrete have three captured live transitions below. Integrated is supported on the basis of the owner's separate explicit hardware-success report; it does not have an equivalent post-boot capture in this record. Warm-restart effectiveness remains unverified.

Software tests and live hardware tests are separate. Record observed results; a passing build does not establish physical MUX switching or guarantee hardware recovery.

## Reference environment

MSI Vector 16 HX AI A2XWIG / MS-15M3; BIOS E15M3IMS.116, EC 15M3EMS1.113. Intel Core Ultra 9 275HX + RTX 5080 Laptop. CachyOS kernel 7.2.2, KDE Plasma 6.7.4 Wayland, Qt 6.11.2, NVIDIA 610.57.04.

No serial numbers, raw firmware, account identifiers, or encryption material belong here.

## Initial read-only observations

- Model, motherboard, BIOS and MSI WMI binding match.
- OEM variable has the expected 20-byte payload and attributes `0x00000007`.
- Requested and current modes decode as MSHybrid; Discrete/Integrated capability bits are present.
- Starting internal eDP route is Intel.

These observations describe the starting state before the live test below. Read-only diagnostics alone do not validate a write.

## Software validation baseline

- [x] Rust formatting, strict Clippy on Linux/Windows targets, and 22 Rust tests pass.
- [x] Failure tests cover ambiguous ACPI completion, durable phases, stale state, and journal recovery classification.
- [x] Qt compilation, parsing, mode gating, localization and safe process lifetime pass: 40 Qt test results, including initialization/cleanup.
- [x] English and Turkish screenshots visually checked at 680×900; no clipped controls.
- [x] Real KDE StatusNotifierItem registration verified in inert demo mode; menu labels, active mode, language switching and clean exit verified over D-Bus.
- [x] Helper restrictions (12 cases) and staged installation/removal tests (14 cases) pass; desktop/AppStream metadata validates.
- [x] Linux and native Windows CI pass on implementation commit `3109232`; [recorded CI run](https://github.com/hayatboj/msi-gpu-mux-switch/actions/runs/34119405294).
- [x] Linux/Windows bundles build from that commit; the Linux archive checksum and complete 16-file payload validate. Both GitHub-built Linux executables launch on CachyOS; [build run](https://github.com/hayatboj/msi-gpu-mux-switch/actions/runs/34119405288).

The counts and CI links above identify the release-candidate implementation baseline, completed on 2026-09-07. They are not a claim that an older CI run built the final `0.3.0` tag. See [CI](https://github.com/hayatboj/msi-gpu-mux-switch/actions/workflows/ci.yml) for the exact commit's remote Linux/Windows results. Demo tests never invoked the privileged helper. The helper has PIE, full RELRO and a non-executable stack. Installation tests used an isolated synthetic system tree.

The release workflow runs only on `v*` tag pushes or manual `workflow_dispatch`, not branch pushes or pull requests. Publication requires a `v*` tag reference and successful Linux and Windows validation/build jobs; the tag must match the Cargo package version. Check the exact tag's [release build](https://github.com/hayatboj/msi-gpu-mux-switch/actions/workflows/release.yml) and downloaded `SHA256SUMS` when installing. GitHub Linux archives build on Ubuntu 24.04 with system Qt 6 and do not bundle Qt.

## Version 0.4.0 software checks

Local validation on 2026-09-07 passed all eight CTest groups: helper restrictions,
installer boundaries, GUI/status/animation/confirmation, desktop power requests,
GNOME detection, About/release notes, inert CLI previews, and local-instance
forwarding. Qt result counts include initialization and cleanup: **55 UI**, **42
power**, **28 desktop integration**, **6 About**, and **4 instance** results.
The launch suite passed two tests with bilingual/page and invalid-argument cases;
the unchanged installer suite passed 17 cases and the helper suite passed 12.
Rust formatting, strict Linux and Windows-target Clippy, and 22 local Rust tests
also passed. Desktop and AppStream metadata validate.

The full demo confirmation-to-power-dialog path was tested without helper/probe
processes or desktop power calls: the hero kept current Hybrid and requested
Discrete separate, all three power choices appeared, Later was the default, and
choosing it sent no power request. English/Turkish main and About/release-note
screens and all six pending directions were visually reviewed. The firmware
engine, helper and installer implementation are unchanged from 0.3.0.

The exact 0.4.0 tag's Linux/Windows workflows remain authoritative for remote
validation and publication. No real firmware or desktop power action was run as
part of these software tests. Native GNOME execution remains outside the observed
KDE session; see the GNOME integration limits below.

## Stable 0.3.0 software checks

Final local checks for the stable desktop and packaging changes passed on 2026-09-07:

- **44 Qt test results**, including initialization/cleanup, covering the desktop's default mode gates, explicit experimental opt-in, and existing status/process behavior.
- **17 installer/removal tests**, using an isolated synthetic system tree.

The historical release-candidate counts above remain unchanged. The stable release's own Linux and Windows workflow jobs gate publication; historical CI runs do not substitute for those checks.

## Live Linux hardware observation

Three transitions in the **MSHybrid → Discrete → MSHybrid → Discrete** sequence succeeded on the reference laptop on **2026-09-07**. All three used the same installed application, **0.3.0-rc.1**, commit [`fda56b8905e988a73d3555b734ada4a38179f64a`](https://github.com/hayatboj/msi-gpu-mux-switch/commit/fda56b8905e988a73d3555b734ada4a38179f64a), with model **Vector 16 HX AI A2XWIG**, board **MS-15M3**, and BIOS **E15M3IMS.116**. The firmware protocol and transaction source in stable **0.3.0** is unchanged from that hardware-tested build. Stable-release changes concern versioning, desktop mode availability, tray handling, and documentation.

The user explicitly approved each transition and performed a manual full shutdown and power-on after it. Privileged diagnostics before the initial transition succeeded with apply-ready clear; the initial request completed its firmware acknowledgement. Read-only post-boot status provided the following observations:

| Transition | Post-boot capture (UTC+03:00) | Current / selected target | Active internal display route |
|---|---|---|---|
| Hybrid → Discrete | 2026-09-07 15:19:10 | `discrete` / `discrete` | `card1-eDP-1`, NVIDIA, `nvidia`, PCI `0000:01:00.0` |
| Discrete → Hybrid | 2026-09-07 15:30:03 | `ms-hybrid` / `ms-hybrid` | `card2-eDP-1`, Intel, `i915`, PCI `0000:00:02.0` |
| Hybrid → Discrete | 2026-09-07 15:36:52 | `discrete` / `discrete` | `card1-eDP-1`, NVIDIA, `nvidia`, PCI `0000:01:00.0` |

All three post-boot observations reported **`pending_shutdown=false`** and **`switching_supported=true`**. KDE retained the native **2560×1600 at 240 Hz** display mode after every transition. After the first Discrete transition, PCI display enumeration showed only NVIDIA. On return to Hybrid, NVIDIA remained present with its `card1-eDP-2` connector disconnected while Intel drove the internal panel. The final observation confirmed a second physical switch of the panel to NVIDIA. These routing observations verify more than a requested target value.

This record summarizes observations without publishing raw firmware or diagnostic captures. It verifies both default directions and a repeated Hybrid-to-Discrete transition on this configuration. It does not establish compatibility with another BIOS or laptop.

## Hardware release gate for stable Hybrid/Discrete support

- [x] Privileged diagnostics succeed with apply-ready clear.
- [x] User explicitly approves each live transition.
- [x] Hybrid to Discrete succeeds on the reference laptop, including acknowledgement.
- [x] Complete manual shutdown and power-on performed after each transition.
- [x] Firmware reports Discrete and the active internal eDP route is NVIDIA-driven.
- [x] Return to Hybrid is verified with the active internal eDP route driven by Intel.
- [x] A second Hybrid to Discrete transition is verified after another full power cycle.
- [x] Native 2560×1600 at 240 Hz is retained in all three observations.
- [x] Tested application commit and BIOS/EC versions are recorded.
- [x] Stable release preserves the tested firmware protocol and transaction engine.

This completes the hardware gate for the default Hybrid/Discrete workflow in **0.3.0** on the exact reference configuration.

## Owner-reported Integrated observation

On **2026-09-07**, the device owner explicitly reported that an Integrated hardware test succeeded on the reference laptop. Version **0.4.0** therefore exposes Integrated alongside Hybrid and Discrete without the previous experimental opt-in.

This is an owner-reported result, separate from the three captured Hybrid/Discrete observations above. The report did not supply a transition direction, application build, boot method, timestamped firmware snapshot or internal-display-route capture. Do not infer those details or turn this report into a fourth entry in the captured test sequence. In particular, it does not establish that a warm restart is sufficient.

## 0.4.0 desktop and session scope

The desktop update adds About/What's new/System information, red Discrete/amber Hybrid/teal Integrated visuals, animated pending-state feedback with optional reduced motion, and a post-success Restart/Power Off/Later choice. Current mode and requested target remain separate until fresh status confirms them. Power requests require an explicit user choice and pass through the active desktop's normal, non-forced session handling; acceptance by that interface is not evidence that a restart or mode change occurred.

GNOME uses the existing StatusNotifierItem menu through the maintained AppIndicator adapter, with application-launcher mode actions opening the normal confirmation flow. The adapter's GNOME Extensions page listed an active version for Shell 45–50 when checked on 2026-09-07. No native GNOME session was available on the development host, so GNOME menu rendering, session-power behavior and notifications are not claimed as locally runtime-validated. See the [GNOME integration guide](https://github.com/hayatboj/msi-gpu-mux-switch/blob/linux-kde/docs/GNOME.md).

The software counts and historical CI links above belong to their named 0.3.0-era baselines. They do not establish final 0.4.0 build or CI results.

## Remaining validation limits

- **Integrated evidence:** the owner's success report is recorded above; an independently captured transition sequence, boot method and internal-display route are not available.
- **Warm restart:** the three captured transitions used full shutdown/power-on cycles. The new Restart choice does not itself validate a warm restart as sufficient for physical MUX switching.
- **GNOME runtime:** the maintained adapter and desktop/session interfaces have not been exercised in a native GNOME session on this development host.
- **Actual firmware failure recovery:** no live failure was deliberately induced. Synthetic fault-injection tests validate software failure handling and uncertain-state reporting; they do not establish a universal hardware recovery method.
- **Other hardware or firmware:** the stable result does not extend to another model, board, BIOS, or uncharacterized ACPI transport.

The earlier 0.3.0 release kept Integrated experimental and outside its default Hybrid/Discrete workflow. That historical restriction is not the current 0.4.0 mode policy. Never intentionally cause a real firmware failure as a release gate. Recovery limits remain documented in [RECOVERY.md](RECOVERY.md).
