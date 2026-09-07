# Security policy

This application controls persistent firmware state. Treat a defect permitting unintended firmware access, an unauthorized operation, a wrong-device write, or misleading recovery status as security-sensitive.

Report sensitive issues through [GitHub private vulnerability reporting](https://github.com/hayatboj/msi-gpu-mux-switch/security/advisories/new), which is enabled for this fork. Do not post raw UEFI dumps, encryption keys, account information, or serial numbers. Public functional reports should contain only the application version, model, motherboard, BIOS/kernel version, and sanitized error message.

## Trust boundaries

The GUI is an ordinary user process. Its preferences and inputs are untrusted by the privileged helper. Mode changes use a fixed absolute helper and a strict allowlist. The helper authorizes no shell commands or arbitrary paths; it runs only the installed root-owned backend with a restricted environment.

Polkit requires administrator authorization per request. The application installs no passwordless rule, setuid executable, user-writable root service, or root desktop process.

The backend enforces hardware/BIOS identity, AC power, firmware layout, mode capabilities, serialization, fresh preflight, durable transaction state, write verification, and reporting of uncertain outcomes. The GUI is never the sole protection.

Installed executables, policies, and parent directories must be root-controlled and not group/world writable. A helper copied without the matching backend and policy is unsupported.

## Recovery boundaries

Restoring the selected mode is not a complete firmware rollback. A firmware method can take effect before its response fails. A crash or power interruption can skip in-process recovery. Durable metadata preserves that uncertainty and prevents an automatic repeat attempt.

Metadata is not a full BIOS backup. Do not commit raw firmware. The CLI and desktop helper provide no hardware or BIOS write bypasses. Integrated is an advanced experimental CLI mode and requires separate desktop settings opt-in; that opt-in does not weaken backend safety checks.

Stable version `0.3.0` covers Hybrid/Discrete switching on the exact Vector 16 HX AI A2XWIG / MS-15M3 / E15M3IMS.116 configuration. Three successful real transitions with full shutdown/power-on cycles are recorded in [the validation record](docs/VALIDATION.md). Integrated switching and recovery from an actual firmware failure have not been validated. Stable status does not promise recovery from every failure or support for other firmware versions.

## Tests

Regression tests must cover rejection paths and partial failures using synthetic inputs and injected failures. Deliberately causing a real firmware failure is not a release requirement. CI must not load writable EC modules, invoke real apply methods, change UEFI variables, or power off a host.
