# MSI GPU MUX Switch

`msi-mux-switch` is a small Windows command-line utility for changing the GPU
MUX mode on the MSI Vector 16 HX AI A2XWIG. It is a clean-room implementation
of the GPU switching function otherwise provided by MSI Center, without
requiring MSI Center to be installed.

The supported modes are:

- MSHybrid (`mshybrid`), which lets Windows use both GPUs as appropriate
- Dedicated graphics (`discrete`), which selects the discrete GPU
- Integrated graphics (`integrated`), which selects the integrated GPU

The utility supports interactive use as well as structured `--json` output for
scripts.

## Supported hardware

This implementation has been characterized and tested on exactly this
configuration:

| Component | Supported value |
| --- | --- |
| Laptop | MSI Vector 16 HX AI A2XWIG |
| Motherboard | MS-15M3 |
| BIOS | E15M3IMS.116 |
| Operating system | Windows |

The program refuses to switch modes when the laptop model or motherboard does
not match. It also refuses unvalidated BIOS versions. Explicit override flags
exist for development and characterization, but they do not make an unknown
machine or firmware version safe.

> [!WARNING]
> This utility writes an OEM UEFI variable and invokes an OEM ACPI control
> method. An incorrect or interrupted firmware operation can leave a machine
> unbootable or require recovery. Keep the BitLocker recovery key available
> off the laptop, connect AC power, save all work, and use the override flags
> only if you understand and have validated the firmware contract.

## Download

Most users should download the prebuilt Windows archive from the
[latest release](https://github.com/steelbrain/msi-gpu-mux-switch/releases)
instead of compiling the project themselves. Download
`windows-amd64-msi-mux-switch.zip`, verify it against the release's
`SHA256SUMS` file, and extract it to obtain `msi-mux-switch.exe`.

The executable's embedded application manifest asks Windows for Administrator
privileges when it starts.

## Usage

Run without a mode to use the interactive selector:

```powershell
.\msi-mux-switch.exe
```

Or name the desired mode directly:

```powershell
.\msi-mux-switch.exe mshybrid
.\msi-mux-switch.exe discrete
.\msi-mux-switch.exe integrated
```

The aliases `hybrid`, `ms-hybrid`, `dgpu`, `igpu`, and `uma` are also
accepted. An interactive mode change requires Administrator privileges, AC
power, and an exact typed confirmation. The program never shuts down or
restarts the laptop itself.

After a successful apply:

1. Save all work and close applications.
2. Perform a full shutdown, for example with `shutdown /s /t 0`.
3. Power the laptop on and run the debug command to verify its current mode.

### Diagnostics

Collect a read-only state report:

```powershell
.\msi-mux-switch.exe --debug
.\msi-mux-switch.exe --debug --json
```

The report includes the detected model, board and BIOS; Secure Boot state; the
decoded MUX state and capabilities; relevant MSI Center registry values; MSI
ACPI state; and Windows display devices. Some firmware diagnostics may be
reported as unavailable when the process is not elevated.

### JSON output

Add `--json` to a mode-switching command to emit newline-delimited JSON events
on standard output:

```powershell
.\msi-mux-switch.exe discrete --json
```

Warnings and errors are emitted on standard error so standard output remains
suitable for a JSON event consumer. When a target mode is supplied with
`--json`, the typed confirmation is skipped so the command can run
non-interactively. Hardware, BIOS, elevation, AC-power, firmware-layout, and
ACPI safety checks still apply. Debug mode emits one formatted JSON document
with `schema_version` set to `1`.

Run `msi-mux-switch.exe --help` for all options, including the deliberately
named `--allow-unsupported-hardware` and `--allow-unvalidated-bios` development
overrides.

## Safety and rollback behavior

Before writing, the utility verifies the variable size and attributes, records
the previous target and a SHA-256 hash under
`%LOCALAPPDATA%\msi-gpu-mux\backups`, and changes only the target-mode bits. It
then reads the complete variable back and verifies it before starting the ACPI
apply handshake.

If the apply sequence fails after staging the target, the utility attempts to
restore the previous target bits. If the firmware trigger had already been
sent, it reports that fact and instructs the user to save work and perform a
full shutdown. The JSON rollback record contains metadata and a hash, not a
copy of the raw firmware variable.

## Development

On Windows, the usual checks are:

```powershell
cargo fmt --check
cargo test
cargo clippy --all-targets -- -D warnings
```

See [AGENTS.md](AGENTS.md) for the protocol invariants and contributor safety
requirements.

## Changelog

Release history is documented in [CHANGELOG.md](CHANGELOG.md).

## Building from source

Most users should use the prebuilt archive from the latest release. To compile
the utility yourself, install Rust 1.85 or newer for Windows using
[rustup](https://rustup.rs/), then build from a Developer Command Prompt or
another terminal with the MSVC build tools available:

```powershell
cargo build --release
```

The resulting executable is
`target\release\msi-mux-switch.exe`. Its embedded application manifest asks
Windows for Administrator privileges.

The project intentionally supports Windows only. Building it for another
operating system produces a compile-time error.

## License

This project is licensed under the [MIT License](LICENSE).
