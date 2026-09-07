use anyhow::{Context, Result, bail};
use clap::{Parser, error::ErrorKind};
use msi_gpu_mux::platform::NativePlatform;
use msi_gpu_mux::transaction::{self, DiskJournal};
use msi_gpu_mux::{
    FirmwareInfo, is_elevated, privilege_requirement, query_machine, read_msi_variable,
    recovery_key_warning, shutdown_command, verification_command, wait_for_keypress,
};
use msi_gpu_mux::{Mode, StateSnapshot, collect_snapshot, collect_status};
use serde_json::{Value, json};
use std::io::{self, Write};

/// Inspect or change the GPU MUX mode on the characterized MSI MS-15M3.
#[derive(Debug, Parser)]
#[command(version, about)]
struct Args {
    /// Target mode. If omitted, an interactive menu is shown.
    #[arg(value_name = "MODE", conflicts_with_all = ["debug", "status"])]
    mode: Option<Mode>,

    /// Collect read-only machine and MUX diagnostics, then exit.
    #[arg(long)]
    debug: bool,

    /// Read status without invoking any ACPI method (safe for desktop polling).
    #[arg(long, conflicts_with = "debug")]
    status: bool,

    /// Emit JSON and skip typed confirmation. Progress is emitted as JSONL events.
    #[arg(long)]
    json: bool,

    /// Permit unsupported hardware only for explicit --debug diagnostics.
    #[arg(long)]
    allow_unsupported_hardware: bool,

    /// Legacy override, rejected for switching in this safety-hardened build.
    #[arg(long)]
    allow_unvalidated_bios: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum RunOutcome {
    Applied,
    Finished,
}

struct Reporter {
    json: bool,
}

impl Reporter {
    fn event(&self, event: &str, message: impl AsRef<str>, data: Value) -> Result<()> {
        let message = message.as_ref();
        if self.json {
            writeln!(
                io::stdout().lock(),
                "{}",
                serde_json::to_string(&json!({
                    "event": event,
                    "message": message,
                    "data": data,
                }))?
            )?;
        } else {
            writeln!(io::stdout().lock(), "{message}")?;
        }
        Ok(())
    }

    fn warning(&self, event: &str, message: impl AsRef<str>, data: Value) {
        let message = message.as_ref();
        if self.json {
            eprintln!(
                "{}",
                json!({
                    "event": event,
                    "level": "warning",
                    "message": message,
                    "data": data,
                })
            );
        } else {
            eprintln!("warning: {message}");
        }
    }

    fn error(&self, message: impl AsRef<str>) {
        let message = message.as_ref();
        if self.json {
            eprintln!(
                "{}",
                json!({
                    "event": "error",
                    "level": "error",
                    "message": message,
                })
            );
        } else {
            eprintln!("error: {message}");
        }
    }

    fn prompt(&self, event: &str, message: &str) -> Result<String> {
        if self.json {
            eprintln!(
                "{}",
                json!({
                    "event": event,
                    "level": "prompt",
                    "message": message,
                })
            );
            io::stderr().flush()?;
        } else {
            print!("{message}");
            io::stdout().flush()?;
        }

        let mut value = String::new();
        io::stdin().read_line(&mut value)?;
        Ok(value.trim().to_string())
    }
}

fn main() {
    let json_requested = std::env::args_os().any(|arg| arg == "--json");
    let args = match Args::try_parse() {
        Ok(args) => args,
        Err(error) => {
            let informational = matches!(
                error.kind(),
                ErrorKind::DisplayHelp | ErrorKind::DisplayVersion
            );
            let exit_code = error.exit_code();
            if json_requested && !informational {
                eprintln!(
                    "{}",
                    json!({
                        "event": "cli_error",
                        "level": "error",
                        "message": error.to_string(),
                    })
                );
            } else {
                let _ = error.print();
            }
            if !informational && !json_requested {
                wait_for_keypress();
            }
            if exit_code != 0 {
                std::process::exit(exit_code);
            }
            return;
        }
    };

    let reporter = Reporter { json: args.json };
    match run(args, &reporter) {
        Ok(outcome) => {
            if outcome == RunOutcome::Applied && !reporter.json {
                wait_for_keypress();
            }
        }
        Err(error) => {
            reporter.error(format!("{error:#}"));
            if !reporter.json {
                wait_for_keypress();
            }
            std::process::exit(1);
        }
    }
}

fn choose_mode(available: &[Mode], reporter: &Reporter) -> Result<Option<Mode>> {
    loop {
        let answer = reporter.prompt("mode_prompt", "Choose a mode number, or Q to cancel: ")?;
        if answer.eq_ignore_ascii_case("q") {
            return Ok(None);
        }
        if let Ok(index) = answer.parse::<usize>()
            && (1..=available.len()).contains(&index)
        {
            return Ok(Some(available[index - 1]));
        }
        reporter.warning("invalid_selection", "Invalid selection.", Value::Null);
    }
}

fn print_debug_text(snapshot: &StateSnapshot) {
    println!("MSI GPU MUX state");
    println!("Captured: {}", snapshot.captured_at);
    println!("Elevated: {}", snapshot.elevated);
    println!(
        "Machine: {} {} (board {} {}, BIOS {})",
        snapshot.machine.manufacturer,
        snapshot.machine.model,
        snapshot.machine.board,
        snapshot.machine.board_revision,
        snapshot.machine.bios
    );
    println!("Expected hardware: {}", snapshot.machine.expected_hardware);
    println!("AC power online: {}", snapshot.ac_power_online);
    println!("Switching supported: {}", snapshot.switching_supported);
    if let Some(reason) = &snapshot.switching_block_reason {
        println!("Switching blocked: {reason}");
    }
    println!("Manual shutdown pending: {}", snapshot.pending_shutdown);
    println!("Internal display routing:");
    for display in &snapshot.internal_displays {
        println!(
            "  {}: {} / enabled={}, {} (driver {})",
            display.connector,
            display.status,
            display.enabled,
            display.vendor,
            display.driver.as_deref().unwrap_or("unknown")
        );
    }

    match (&snapshot.secure_boot.value, &snapshot.secure_boot.error) {
        (Some(enabled), _) => println!("Secure Boot: {enabled}"),
        (_, Some(error)) => println!("Secure Boot: unavailable ({error})"),
        _ => println!("Secure Boot: unavailable"),
    }

    println!("Registry:");
    for (name, value) in &snapshot.registry {
        match value {
            Some(value) => println!("  {name}: {value}"),
            None => println!("  {name}: unavailable"),
        }
    }

    match (&snapshot.firmware.value, &snapshot.firmware.error) {
        (Some(firmware), _) => {
            println!("Firmware:");
            println!(
                "  MsiDCVarData: length {}, attributes {}, byte 5 {}",
                firmware.length, firmware.attributes, firmware.byte5
            );
            println!(
                "  Selected target: {} ({})",
                firmware
                    .selected_target_mode
                    .map_or_else(|| "unknown".to_string(), |mode| mode.to_string()),
                firmware.selected_target_value
            );
            println!(
                "  Current mode: {} ({})",
                firmware
                    .current_mode
                    .map_or_else(|| "unknown".to_string(), |mode| mode.to_string()),
                firmware.current_value
            );
            println!(
                "  Capabilities: new-switch={}, discrete={}, integrated={}",
                firmware.new_switch_supported,
                firmware.discrete_supported,
                firmware.integrated_supported
            );
        }
        (_, Some(error)) => println!("Firmware: unavailable ({error})"),
        _ => println!("Firmware: unavailable"),
    }

    match (&snapshot.wmi_ap.value, &snapshot.wmi_ap.error) {
        (Some(ap), _) => {
            println!("MSI ACPI Get_AP:");
            println!("  Raw prefix: {}", ap.raw_prefix);
            println!("  Data byte 1: 0x{:02X}", ap.data_byte1);
            println!("  Apply-ready: {}", ap.apply_ready);
        }
        (_, Some(error)) => println!("MSI ACPI Get_AP: unavailable ({error})"),
        _ => println!("MSI ACPI Get_AP: unavailable"),
    }

    println!("Display devices:");
    for device in &snapshot.display_devices {
        println!(
            "  {}: present={:?}, status={}, problem={:?}",
            device.friendly_name,
            device.present,
            device.status.as_deref().unwrap_or("unknown"),
            device.problem_code
        );
        for hardware_id in &device.hardware_ids {
            println!("    {hardware_id}");
        }
    }
}

fn run(args: Args, reporter: &Reporter) -> Result<RunOutcome> {
    if args.debug || args.status {
        let snapshot = if args.status {
            collect_status()?
        } else {
            collect_snapshot(args.allow_unsupported_hardware)?
        };
        if args.json {
            println!("{}", serde_json::to_string_pretty(&snapshot)?);
        } else {
            print_debug_text(&snapshot);
        }
        return Ok(RunOutcome::Finished);
    }

    if args.json && args.mode.is_none() {
        bail!(
            "JSON mode requires a target MODE, --status, or --debug; interactive input is disabled"
        );
    }
    if args.allow_unsupported_hardware || args.allow_unvalidated_bios {
        bail!(
            "write overrides are disabled in this safety-hardened build; --allow-unsupported-hardware is available only with --debug"
        );
    }
    transaction::validate_machine(&query_machine()?)?;
    let initial_variable = read_msi_variable()?;
    let initial_info = FirmwareInfo::decode(&initial_variable)?;
    if !initial_info.new_switch_supported {
        bail!("firmware does not advertise the characterized three-mode switch");
    }
    let available = initial_info.available_modes();
    let current = initial_info
        .current_mode
        .context("firmware current-mode bits are not recognized")?;

    reporter.event(
        "current_mode",
        format!("Current mode: {current}"),
        json!({ "mode": current, "value": current.value() }),
    )?;
    let menu = available
        .iter()
        .enumerate()
        .map(|(index, mode)| format!("  {}. {mode}", index + 1))
        .collect::<Vec<_>>()
        .join("\n");
    reporter.event(
        "available_modes",
        format!("Available modes:\n{menu}"),
        json!({ "modes": available }),
    )?;

    let target = match args.mode {
        Some(mode) => mode,
        None => match choose_mode(&available, reporter)? {
            Some(mode) => mode,
            None => {
                reporter.event("cancelled", "Cancelled. No state was changed.", Value::Null)?;
                return Ok(RunOutcome::Finished);
            }
        },
    };
    if !available.contains(&target) {
        bail!("mode '{target}' is not advertised as supported by this firmware");
    }

    let selected_target = initial_info
        .selected_target_mode
        .context("firmware selected/target bits are not recognized")?;
    if target == current && target == selected_target {
        reporter.event(
            "unchanged",
            format!("The machine is already in {target} mode. No state was changed."),
            json!({ "mode": target, "value": target.value() }),
        )?;
        return Ok(RunOutcome::Finished);
    }

    reporter.event(
        "selected_mode",
        format!("Selected mode: {target}"),
        json!({ "mode": target, "value": target.value() }),
    )?;
    reporter.event(
        "shutdown_required",
        "A full manual shutdown is required after a successful apply.\nThis program will not shut down or reboot the computer.",
        json!({ "automatic_shutdown": false }),
    )?;

    if !is_elevated()? {
        bail!("the GPU mode selector requires {}", privilege_requirement());
    }
    if !msi_gpu_mux::ac_power_online()? {
        bail!("AC power is required for a GPU mode transition");
    }

    reporter.warning("bitlocker_reminder", recovery_key_warning(), Value::Null);
    if args.json {
        reporter.event(
            "confirmation_skipped",
            "Typed confirmation skipped in JSON mode.",
            json!({ "json_mode": true }),
        )?;
    } else {
        let expected_confirmation = target.confirmation();
        let confirmation = reporter.prompt(
            "confirmation_prompt",
            &format!("Type '{expected_confirmation}' exactly to continue: "),
        )?;
        if confirmation != expected_confirmation {
            reporter.event(
                "confirmation_rejected",
                "Confirmation did not match. No state was changed.",
                Value::Null,
            )?;
            return Ok(RunOutcome::Finished);
        }
    }

    let mut journal = DiskJournal::native()?;
    transaction::apply::<NativePlatform, _>(target, &mut journal, |event, message, data| {
        // Console I/O is not allowed to abort a hardware critical section.
        // A closed output pipe leaves the durable journal as the source of truth.
        let _ = reporter.event(event, message, data);
    })?;

    let shutdown_command = shutdown_command();
    let verification_command = verification_command();
    reporter.event(
        "success",
        format!(
            "Mode '{target}' is staged and the firmware apply handshake succeeded.\nSave all work, close applications, then perform a full shutdown manually.\nRecommended manual command after saving work: {shutdown_command}\nAfter powering on, run {verification_command} to verify the new current mode."
        ),
        json!({
            "mode": target,
            "value": target.value(),
            "manual_shutdown_required": true,
            "verification_command": verification_command,
        }),
    )?;
    Ok(RunOutcome::Applied)
}
