# Security policy

This application controls persistent firmware state. Treat a defect permitting unintended firmware access, an unauthorized operation, a wrong-device write, or misleading recovery status as security-sensitive.

Report sensitive issues through [GitHub private vulnerability reporting](https://github.com/hayatboj/msi-gpu-mux-switch/security/advisories/new), which is enabled for this fork. Do not post raw UEFI dumps, encryption keys, account information, or serial numbers. Public functional reports should contain only the application version, model, motherboard, BIOS/kernel version, and sanitized error message.

## Trust boundaries

The GUI is an ordinary user process. Its preferences and inputs are untrusted by the privileged helper. Mode changes use a fixed absolute helper and a strict allowlist. The helper authorizes no shell commands or arbitrary paths; it runs only the installed root-owned backend with a restricted environment.

Polkit requires administrator authorization per request. The application installs no passwordless rule, setuid executable, user-writable root service, or root desktop process.

The backend enforces hardware/BIOS identity, AC power, firmware layout, mode capabilities, serialization, fresh preflight, durable transaction state, write verification, and reporting of uncertain outcomes. The GUI is never the sole protection.

Apply saves a local user-owned draft, without invoking the helper or requesting administrator authorization. This draft is not a firmware transaction, a hardware success record, or permission to apply later. Reopening the application may restore it only within the same boot; a draft from a different boot must be invalidated, never replayed or treated as successfully applied. Invalid draft data cannot authorize a write. Selecting the current mode or canceling a draft clears only that local choice.

Committing a draft requires a fresh explicit Restart/Power Off choice inside MSI MUX. The application rechecks the current status and latest draft, invokes the restricted helper with administrator authorization, and requires verified firmware success before requesting the chosen desktop power action. Later performs neither operation. Restarting or shutting down outside MSI MUX does not commit a draft. A failed, canceled or uncertain apply must not reach the power transport.

The application asks the active desktop's normal session interface and preserves its confirmation, inhibitors and cancellation. It does not force a power action, bypass the desktop through a direct logind request, or retry through another transport after a failure or uncertain reply. A desktop accepting a request does not prove that it completed or that the physical MUX changed. If the desktop cancels after a successful apply, the firmware-pending target remains; the local draft flow cannot undo it.

Launcher and local IPC mode requests open the normal local-draft flow; they authorize neither firmware nor power operations. GNOME's AppIndicator adapter displays the existing Qt indicator/menu and receives no separate firmware privilege. Desktop detection only controls presentation and setup guidance. The CLI's separate immediate-apply path retains mode-specific typed confirmation in human-readable mode and all backend checks; it does not participate in draft persistence. Scripted `--json` mode skips only that prompt.

Installed executables, policies, and parent directories must be root-controlled and not group/world writable. A helper copied without the matching backend and policy is unsupported.

## Recovery boundaries

Restoring the selected mode is not a complete firmware rollback. A firmware method can take effect before its response fails. A crash or power interruption can skip in-process recovery. Durable metadata preserves that uncertainty and prevents an automatic repeat attempt.

Metadata is not a full BIOS backup. Do not commit raw firmware. The CLI and desktop helper provide no hardware or BIOS write bypasses. All three modes, including Integrated, retain the same backend safety checks.

Do not confuse editable drafts with existing firmware-pending transactions. An acknowledged target from a previous application version, the CLI, or an earlier commit in this boot cannot be replaced by changing a draft. Preserve the pending-state and apply-ready gates. Clearing user preferences or dismissing a dialog must never erase root-controlled transaction metadata or imply hardware rollback.

Version `0.5.0` offers local drafts for Hybrid, Discrete and Integrated on the exact Vector 16 HX AI A2XWIG / MS-15M3 / E15M3IMS.116 configuration. Three Hybrid/Discrete transitions with full shutdown/power-on cycles are recorded in [the validation record](docs/VALIDATION.md). The owner separately reported Integrated hardware success; its direction and boot method were not captured. Warm-restart effectiveness, retargeting an acknowledged firmware transaction, and recovery from an actual firmware failure remain unverified. Release status does not promise recovery from every failure or support for other firmware versions.

## Tests

Regression tests must cover rejection paths and partial failures using synthetic inputs and injected failures. Cover draft replacement/cancellation, same-boot restoration, new-boot invalidation, immutable firmware-pending targets, and the requirement for verified helper success before a power request. Deliberately causing a real firmware failure is not a release requirement. CI must not load writable EC modules, invoke real apply methods, change UEFI variables, or send real restart/shutdown requests. Session power tests use injected transports. Native GNOME behavior requires separate session testing and is not established by synthetic tests.
