# Project Agent Instructions

## Scope

This repository contains a Rust command-line utility that implements GPU MUX
diagnostics and switching on Windows and Linux for the characterized MSI Vector
16 HX AI A2XWIG. The known motherboard is MS-15M3 and the validated BIOS is
E15M3IMS.116. Version 0.4.0 offers Hybrid, Discrete and Integrated as ordinary
desktop modes on that exact configuration. Three Hybrid/Discrete full
shutdown/power-on transitions are recorded in `docs/VALIDATION.md`; the owner
separately reported a successful Integrated hardware test. Do not invent that
test's transition direction, boot method, application build, or post-boot capture.
Capability bits alone do not establish physical success.

On Linux, UEFI access uses efivarfs and MSI ACPI access uses only the in-tree
`msi-wmi-platform` driver's root-only debugfs interface. Do not add a direct AML
call or a fallback transport without a new, deliberate safety review.

Treat all firmware writes and MSI ACPI calls as safety-critical. Prefer an
explicit failure over guessing a hardware contract or continuing from an
ambiguous state.

## Repository map

- `src/lib.rs` contains mode encoding, shared protocol validation, diagnostics,
  and platform-independent tests.
- `src/platform/mod.rs` defines the shared platform and ACPI traits and selects
  the native implementation. `src/platform/windows.rs` and
  `src/platform/linux.rs` contain all OS-specific system access.
- `src/transaction.rs` contains the tested transaction state machine, durable
  journal, uncertain-completion handling, and target restoration.
- `src/bin/msi-mux-switch.rs` contains CLI parsing, reporting and confirmation.
- `gui/` contains the Qt 6 tray/status window, About/release notes, desktop
  integration, session power requests, translations and tests.
- `packaging/` contains the privileged helper, Polkit policy, desktop metadata,
  Arch package, and validated installer/uninstaller.
- `scripts/` contains native build and bundle creation entry points.
- `build.rs` and `resources/` embed the Windows manifest that requires
  Administrator privileges.

Keep reusable protocol and system-access code in the library. Keep user
interaction in the binary and GUI; keep transaction orchestration testable in
the library.

## Firmware contract and invariants

Do not change these details without new, deliberate characterization and tests:

- The UEFI variable is `MsiDCVarData`, GUID
  `{DD96BAAF-145E-4F56-B1CF-193256298E99}`.
- A writable value must be exactly 20 bytes and have attributes `0x00000007`.
- Byte 5 bits 0-1 are the selected target: `0` MSHybrid, `1` discrete, and `2`
  integrated.
- Byte 5 bits 2-3 report the current mode.
- Staging a target must preserve every other bit and byte.
- Byte 5 bit 4 advertises the characterized switch interface. Bit 5 advertises
  integrated support. A clear bit 6 advertises discrete support.
- `MSI_ACPI` must resolve to instance `ACPI\\PNP0C14\\0_0` in `ROOT\\WMI`.
- `Get_AP` must report success before its data is used. Its returned data byte
  1 is byte 2 of the package, and bit 1 is the apply-ready flag.
- Derive the trigger value from the latest `Get_AP` data byte by preserving its
  upper six bits and setting its low two bits to `01`.
- The apply trigger is `Set_Data(0xD1, trigger)`. After apply-ready asserts, the
  acknowledgement is `Set_Data(0xBE, 0x02)`.

Read the live state again immediately before a write. Verify the complete value
and its attributes after a write. Do not silently coerce unknown mode values,
variable layouts, ACPI results, or capabilities.

## Required safety behavior

Preserve these protections when changing the switching flow:

- Gate writes to the exact model, board and BIOS. Do not provide write bypasses.
- Require root on Linux or Administrator on Windows, and online AC power.
- Require the mode-specific typed confirmation for human-readable interactive
  switching. JSON mode deliberately skips this prompt for scripted use.
- Refuse to start when apply-ready is already asserted.
- Persist transaction metadata before attempting firmware or ACPI writes.
- Serialize access to the Linux debugfs ACPI transport across processes.
- Record a trigger attempt before invoking it; failure to receive its response
  does not prove that the firmware ignored the command.
- Restore the previous target when a post-write step fails, then verify the
  restoration. Report restoration failures without hiding the original error.
- Warn prominently when a trigger was sent before a later failure.
- Never automatically reboot or shut down the user's machine. Only an explicit
  user choice may request Restart or Power Off through the active desktop's
  normal session interface, preserving its confirmation, inhibitors and
  cancellation. Do not force power actions, call direct logind power methods,
  or try a fallback after rejection or an uncertain reply. Offer Later as well.
  Full shutdown/power-on is the recorded hardware path; warm-restart
  effectiveness remains unverified. Keep current mode and requested target
  distinct until fresh post-boot observation.
- Keep routine `--status` polling unprivileged and free of ACPI calls.
- A target restoration is not a guarantee of full hardware rollback.
- Keep the GUI unprivileged. Invoke only the fixed installed Polkit helper.
- Apply the same backend identity, capability, power and transaction checks to
  all three desktop modes. Launcher/IPC `--request-mode` requests must open the
  normal typed confirmation flow and cannot authorize a firmware write.
- GNOME uses the maintained AppIndicator adapter for the existing tray/menu.
  Desktop detection is a presentation hint, never a permission check. Do not
  install or enable Shell extensions automatically, or duplicate firmware
  control inside a Shell extension.

Keep probes and tests that can mutate firmware opt-in and separate from the
normal unit-test suite. Never exercise real firmware writes merely to validate
a refactor.

## CLI and JSON compatibility

Treat scripted output as a public interface:

- With `--json`, mode switching emits one JSON object per line on stdout.
- Warnings, prompts, CLI errors, and runtime errors go to stderr.
- Do not print non-JSON text to stdout in JSON mode.
- Debug JSON is a single `StateSnapshot` document. Increment `schema_version`
  when making an incompatible schema change.
- `--json` deliberately skips typed confirmation. It must not weaken elevation,
  power, hardware, BIOS, firmware validation, or ACPI preflight checks.

Human-readable errors should say what failed and whether state may have
changed. Preserve stable event names unless a compatibility break is intended
and documented. Record user-visible changes under the `Unreleased` section in
`CHANGELOG.md`.

## Clean-room and repository hygiene

Static inspection may be used to determine an unclear contract, but inspected
binaries and material reproduced from their bytes must remain local and
untracked. Do not record the identity or provenance of an inspected binary in
source, documentation, tests, fixtures, commit messages, or logs. Persist only
the resulting independently stated contract and original test evidence.

Do not commit raw UEFI variable contents or machine-specific diagnostic
captures. Tests should use small synthetic values that express only the
contract under test.

## Required validation

Before completing any Rust change, both formatting and Clippy must pass with no
warnings:

```text
cargo fmt --all --check
cargo clippy --locked --all-targets -- -D warnings
```

Run the test suite on Windows as well:

```text
cargo test --locked
```

The crate supports Linux and Windows. From Linux, also install a Windows
standard library and give Clippy an explicit Windows target:

```text
rustup target add x86_64-pc-windows-msvc
cargo clippy --locked --target x86_64-pc-windows-msvc --all-targets -- -D warnings
```

The build script skips Windows resource compilation only when Cargo is running
Clippy. Normal Windows builds must always compile and embed the required
Administrator manifest.

For protocol changes, add focused tests proving that unrelated bits and bytes
remain unchanged, invalid layouts are rejected, and all new decoded values are
handled explicitly. Review the human and JSON paths together.

For Linux integration, run `scripts/build-linux.sh` and
`scripts/package-linux.sh`. Check both UI languages using the inert `--demo`
mode. Never switch a real MUX, request administrator access, or send real
desktop power actions as an automated test. Use injected power transports.
Synthetic desktop detection and StatusNotifierItem tests do not establish
native GNOME session behavior; record that limitation explicitly.

## CI and releases

- `.github/workflows/ci.yml` runs formatting, strict Clippy, tests, and release
  builds for Windows AMD64 and Linux AMD64 on pull requests and relevant branch
  pushes.
- `.github/workflows/release.yml` runs only for `v*` tag pushes or manual
  `workflow_dispatch`. It builds and retains Windows AMD64 and Linux AMD64
  bundles. Publication requires a `v*` tag reference and successful validation
  and build jobs for both platforms; the Linux tag must match the Cargo version.
  The archives and aggregate `SHA256SUMS` are then published as a GitHub Release.
  The workflow does not create the tag.
- Linux archives contain the tray, backend, restricted helper, desktop metadata,
  validated installer/uninstaller and checksums. Windows archives retain the CLI.
- Release scope is the exact documented model/board/BIOS. Preserve the
  distinction between the three captured Hybrid/Discrete transitions and the
  owner's Integrated success report. Historical 0.3.0 experimental-mode limits
  are release history, not current 0.4.0 requirements. New hardware or protocol
  changes need their own evidence; do not generalize existing results. CI cannot
  establish physical MUX behavior or warm-restart effectiveness.
- Use synthetic fault injection to validate failure handling. Do not require or
  intentionally cause real firmware failure as a stable-release gate. Recovery
  limits must remain explicit even for a stable release.
- Keep external GitHub Actions pinned to full commit hashes with a version
  comment. Do not replace pins with movable tags or branches.
