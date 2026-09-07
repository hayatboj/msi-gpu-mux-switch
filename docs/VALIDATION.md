# Validation record

**Version 0.5.0 scope:** Apply saves an editable local draft for Hybrid, Discrete or Integrated on MSI Vector 16 HX AI A2XWIG / MS-15M3 / E15M3IMS.116. Only an explicit Restart/Power Off choice inside MSI MUX commits the latest draft, with fresh checks and verified helper success before the desktop power request. Hybrid/Discrete have three captured live transitions below. Integrated has the owner's separate explicit hardware-success report, without an equivalent post-boot capture here. Warm-restart effectiveness remains unverified.

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

## Version 0.5.0 local-draft contract

The new desktop flow separates a local draft from both the current hardware mode and a firmware-pending target. Apply saves or replaces the draft without administrator authentication, UEFI writes or MSI ACPI calls. Later performs neither firmware nor power operations. Canceling the draft or selecting the current mode clears it. Reopening the application in the same boot restores a valid draft; a new boot invalidates it without inferring that it was applied. Restarting or powering off outside MSI MUX does not commit it.

An explicit Restart/Power Off choice inside the application uses the latest draft, reads fresh status, rechecks eligibility and invokes the restricted helper. Only verified firmware success permits the selected native desktop power request. A failed or uncertain apply sends no power request. Desktop cancellation after success does not undo the firmware-pending target.

Existing acknowledged transactions remain protected from retargeting, including those created by older application versions or the CLI. Draft editing must not erase or replace their journal state. The CLI retains its immediate-apply behavior and mode-specific human-readable confirmation. This desktop change adds no new physical MUX, warm-restart or native-GNOME runtime evidence. The software counts below are historical results for their named versions, not final 0.5.0 results.

## Version 0.5.0 software checks

Local validation on 2026-09-07 passed all nine CTest groups. Qt result counts
include initialization/cleanup: **67 UI**, **65 selection/backend**, **42 power**,
**28 desktop integration**, **6 About**, and **4 instance** results. The suite
also passed **17 installer**, **12 helper rejection**, and **3 inert launch**
tests with bilingual and invalid-argument subcases. Rust formatting, strict Linux
and Windows-target Clippy, and all **22 Rust tests** passed. Desktop metadata,
AppStream validation, bundle inventory and the 16-file offline package check passed.

Coverage includes replacing an Integrated draft with Hybrid before one commit,
Later/Return cancellation, all six visual directions without changing firmware
status, same-boot restoration and new-boot invalidation, changed state and AC
power, preflight/postflight errors, rejected nested commits and duplicate/late
completion. Native-mode fixtures replace process launch with a private no-exec
callback; no test can start the real privileged helper. Only synthetic result
injection and demo flows exercise commit completion and desktop power dispatch.
English/Turkish main, confirmation and About/release-note previews were reviewed.

The exact 0.5.0 tag's Linux/Windows workflows gate publication. The firmware
engine, privileged helper, installer and root transaction journal implementation
remain unchanged from 0.4.0; these software tests add no physical MUX, warm-restart,
or native GNOME evidence.

## Version 0.4.0 software checks

Local validation on 2026-09-07 passed all eight CTest groups: helper restrictions,
installer boundaries, GUI/status/animation/confirmation, desktop power requests,
GNOME detection, About/release notes, inert CLI previews, and local-instance
forwarding. Qt result counts include initialization and cleanup: **56 UI**, **42
power**, **28 desktop integration**, **6 About**, and **4 instance** results.
The launch suite passed two tests with bilingual/page and invalid-argument cases;
the unchanged installer suite passed 17 cases and the helper suite passed 12.
Rust formatting, strict Linux and Windows-target Clippy, and 22 local Rust tests
also passed. Desktop and AppStream metadata validate.

The full demo confirmation-to-power-dialog path was tested without helper/probe
processes or desktop power calls: the hero kept current Hybrid and requested
Discrete separate, all three power choices appeared, Later was the default, and
choosing it sent no power request. A second case presses Return without choosing a power action and also sends no power request; power buttons cannot become implicit defaults on older Qt versions. English/Turkish main and About/release-note
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

## 0.4.0 desktop and session scope (historical)

The 0.4.0 desktop update added About/What's new/System information, red Discrete/amber Hybrid/teal Integrated visuals, animated pending-state feedback with optional reduced motion, and a post-success Restart/Power Off/Later choice. In that release, the firmware apply preceded this power dialog. Version 0.5.0 instead saves a local draft first and commits it only after an explicit in-application power choice. Current mode and requested target remain separate until fresh status confirms them. Acceptance by a desktop power interface is not evidence that a restart or mode change occurred.

GNOME uses the existing StatusNotifierItem menu through the maintained AppIndicator adapter, with application-launcher mode actions opening the normal confirmation flow. The adapter's GNOME Extensions page listed an active version for Shell 45–50 when checked on 2026-09-07. No native GNOME session was available on the development host, so GNOME menu rendering, session-power behavior and notifications are not claimed as locally runtime-validated. See the [GNOME integration guide](https://github.com/hayatboj/msi-gpu-mux-switch/blob/linux-kde/docs/GNOME.md).

Historical CI links and counts in the named 0.3.0-era sections do not establish final 0.4.0 build or CI results. The separate 0.4.0 section records that release's checks.

## Retargeting an acknowledged transaction

Public-source review on **2026-09-07** found no documented contract for replacing an acknowledged firmware target before reboot. Upstream commit `251f474abf88c4e77a94c1dc23a77af79ff44955` rejects `apply_ready=true` before any write. Its normal sequence stages the UEFI target, triggers `Set_Data(0xD1, …)`, waits for readiness, then sends `Set_Data(0xBE, 0x02)`. It does not verify that ACK clears readiness or test that another target can safely replace the pending one. [Upstream preflight](https://github.com/steelbrain/msi-gpu-mux-switch/blob/251f474abf88c4e77a94c1dc23a77af79ff44955/src/bin/msi-mux-switch.rs#L436), [trigger and ACK](https://github.com/steelbrain/msi-gpu-mux-switch/blob/251f474abf88c4e77a94c1dc23a77af79ff44955/src/bin/msi-mux-switch.rs#L533).

Upstream's error path attempts to restore the previous UEFI target, but warns that a sent trigger may already have changed machine state. That fallback is not evidence of safe retargeting after successful ACK. [Upstream failure handling](https://github.com/steelbrain/msi-gpu-mux-switch/blob/251f474abf88c4e77a94c1dc23a77af79ff44955/src/bin/msi-mux-switch.rs#L584).

The [Linux WMI documentation](https://docs.kernel.org/wmi/devices/msi-wmi-platform.html) defines method packaging, without D1/BE retarget semantics. [MSI's user guide](https://us.msi.com/faq/8805) describes selecting a mode and restarting, without explaining replacement of an acknowledged target. These sources do not establish whether firmware uses the latest UEFI value at boot or retains a value from an earlier handshake. Windows UI reselection alone does not answer that question.

Version 0.5.0 allows reselection by keeping it local until commit. It does not weaken the pending-transaction or apply-ready gates, clear readiness flags, or rewrite an acknowledged target.

## Remaining validation limits

- **Integrated evidence:** the owner's success report is recorded above; an independently captured transition sequence, boot method and internal-display route are not available.
- **Warm restart:** the three captured transitions used full shutdown/power-on cycles. The new Restart choice does not itself validate a warm restart as sufficient for physical MUX switching.
- **Retargeting after ACK:** no verified firmware contract permits replacing a committed pending target before boot; editable local drafts are a separate state.
- **GNOME runtime:** the maintained adapter and desktop/session interfaces have not been exercised in a native GNOME session on this development host.
- **Actual firmware failure recovery:** no live failure was deliberately induced. Synthetic fault-injection tests validate software failure handling and uncertain-state reporting; they do not establish a universal hardware recovery method.
- **Other hardware or firmware:** the stable result does not extend to another model, board, BIOS, or uncharacterized ACPI transport.

The earlier 0.3.0 release kept Integrated experimental and outside its default Hybrid/Discrete workflow. That historical restriction is not the current 0.5.0 mode policy. Never intentionally cause a real firmware failure as a release gate. Recovery limits remain documented in [RECOVERY.md](RECOVERY.md).
