use std::collections::BTreeMap;
use std::fmt;
use std::str::FromStr;

use anyhow::{Context, Result, bail};
use chrono::Local;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

pub mod platform;
pub mod transaction;

pub use platform::{
    MsiAcpi, ac_power_online, backup_directory, is_elevated, privilege_requirement,
    query_display_devices, query_machine, query_registry_state, read_msi_variable,
    recovery_key_warning, secure_boot_enabled, shutdown_command, verification_command,
    wait_for_keypress, write_msi_variable,
};

pub const EXPECTED_MODEL: &str = "Vector 16 HX AI A2XWIG";
pub const EXPECTED_BOARD: &str = "MS-15M3";
pub const EXPECTED_BIOS: &str = "E15M3IMS.116";
pub const MSI_VARIABLE_NAME: &str = "MsiDCVarData";
pub const MSI_VARIABLE_GUID: &str = "{DD96BAAF-145E-4F56-B1CF-193256298E99}";
pub const EXPECTED_VARIABLE_LENGTH: usize = 20;
pub const EXPECTED_VARIABLE_ATTRIBUTES: u32 = 0x0000_0007;

pub(crate) const SECURE_BOOT_VARIABLE_NAME: &str = "SecureBoot";
pub(crate) const EFI_GLOBAL_VARIABLE_GUID: &str = "{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}";

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum Mode {
    MsHybrid,
    Discrete,
    Integrated,
}

impl Mode {
    pub const ALL: [Self; 3] = [Self::MsHybrid, Self::Discrete, Self::Integrated];

    pub const fn value(self) -> u8 {
        match self {
            Self::MsHybrid => 0,
            Self::Discrete => 1,
            Self::Integrated => 2,
        }
    }

    pub const fn from_value(value: u8) -> Option<Self> {
        match value {
            0 => Some(Self::MsHybrid),
            1 => Some(Self::Discrete),
            2 => Some(Self::Integrated),
            _ => None,
        }
    }

    pub fn confirmation(self) -> &'static str {
        match self {
            Self::MsHybrid => "APPLY MSHYBRID",
            Self::Discrete => "APPLY DISCRETE",
            Self::Integrated => "APPLY INTEGRATED",
        }
    }
}

impl fmt::Display for Mode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::MsHybrid => write!(f, "MSHybrid"),
            Self::Discrete => write!(f, "Discrete"),
            Self::Integrated => write!(f, "Integrated"),
        }
    }
}

impl FromStr for Mode {
    type Err = anyhow::Error;

    fn from_str(value: &str) -> Result<Self> {
        match value.to_ascii_lowercase().as_str() {
            "mshybrid" | "ms-hybrid" | "hybrid" => Ok(Self::MsHybrid),
            "discrete" | "dgpu" => Ok(Self::Discrete),
            "integrated" | "igpu" | "uma" => Ok(Self::Integrated),
            _ => bail!("unknown GPU mode '{value}'"),
        }
    }
}

#[derive(Debug, Clone)]
pub struct FirmwareVariable {
    pub bytes: Vec<u8>,
    pub attributes: u32,
}

#[derive(Debug, Clone, Serialize)]
pub struct FirmwareInfo {
    pub length: usize,
    pub attributes: String,
    pub byte5: String,
    pub selected_target_value: u8,
    pub selected_target_mode: Option<Mode>,
    pub current_value: u8,
    pub current_mode: Option<Mode>,
    pub new_switch_supported: bool,
    pub integrated_supported: bool,
    pub discrete_supported: bool,
    pub bit7: bool,
}

impl FirmwareInfo {
    pub fn decode(variable: &FirmwareVariable) -> Result<Self> {
        let byte5 = *variable
            .bytes
            .get(5)
            .context("MsiDCVarData is shorter than six bytes")?;
        let target = byte5 & 0x03;
        let current = (byte5 & 0x0c) >> 2;
        Ok(Self {
            length: variable.bytes.len(),
            attributes: format!("0x{:08X}", variable.attributes),
            byte5: format!("0x{byte5:02X}"),
            selected_target_value: target,
            selected_target_mode: Mode::from_value(target),
            current_value: current,
            current_mode: Mode::from_value(current),
            new_switch_supported: byte5 & 0x10 != 0,
            integrated_supported: byte5 & 0x20 != 0,
            discrete_supported: byte5 & 0x40 == 0,
            bit7: byte5 & 0x80 != 0,
        })
    }

    pub fn available_modes(&self) -> Vec<Mode> {
        if !self.new_switch_supported {
            return Vec::new();
        }
        let mut modes = vec![Mode::MsHybrid];
        if self.discrete_supported {
            modes.push(Mode::Discrete);
        }
        if self.integrated_supported {
            modes.push(Mode::Integrated);
        }
        modes
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct MachineInfo {
    pub manufacturer: String,
    pub model: String,
    pub board: String,
    pub board_revision: String,
    pub bios: String,
    pub expected_hardware: bool,
}

#[derive(Debug, Clone, Serialize)]
pub struct DisplayDevice {
    pub friendly_name: String,
    pub status: Option<String>,
    pub problem_code: Option<u32>,
    pub present: Option<bool>,
    pub hardware_ids: Vec<String>,
}

#[derive(Debug, Clone, Serialize)]
pub struct ApState {
    pub raw_prefix: String,
    pub flag: u8,
    pub data_byte1: u8,
    pub apply_ready: bool,
}

pub(crate) fn decode_ap_state(raw: &[u8]) -> Result<ApState> {
    if raw.len() < 3 || raw[0] == 0 {
        bail!("MSI ACPI Get_AP returned an unsuccessful or short package");
    }
    let prefix_len = raw.len().min(8);
    let raw_prefix = raw[..prefix_len]
        .iter()
        .map(|byte| format!("{byte:02X}"))
        .collect::<Vec<_>>()
        .join(" ");
    Ok(ApState {
        raw_prefix,
        flag: raw[0],
        data_byte1: raw[2],
        apply_ready: raw[2] & 0x02 != 0,
    })
}

#[derive(Debug, Clone, Serialize)]
pub struct AvailableSection<T> {
    pub available: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub value: Option<T>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
}

impl<T> AvailableSection<T> {
    fn from_result(result: Result<T>) -> Self {
        match result {
            Ok(value) => Self {
                available: true,
                value: Some(value),
                error: None,
            },
            Err(error) => Self {
                available: false,
                value: None,
                error: Some(format!("{error:#}")),
            },
        }
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct InternalDisplay {
    pub connector: String,
    pub status: String,
    pub enabled: bool,
    pub vendor: String,
    pub driver: Option<String>,
    pub pci_address: Option<String>,
}

#[derive(Debug, Clone, Serialize)]
pub struct StateSnapshot {
    pub schema_version: u32,
    pub captured_at: String,
    pub elevated: bool,
    pub machine: MachineInfo,
    pub secure_boot: AvailableSection<bool>,
    pub registry: BTreeMap<String, Option<u32>>,
    pub firmware: AvailableSection<FirmwareInfo>,
    pub wmi_ap: AvailableSection<ApState>,
    pub display_devices: Vec<DisplayDevice>,
    pub ac_power_online: bool,
    pub internal_displays: Vec<InternalDisplay>,
    pub switching_supported: bool,
    pub switching_block_reason: Option<String>,
    pub switching_block_code: Option<String>,
    pub pending_shutdown: bool,
}

pub fn stage_target(bytes: &[u8], target: Mode) -> Result<Vec<u8>> {
    if bytes.len() <= 5 {
        bail!("MsiDCVarData is shorter than six bytes");
    }
    let mut staged = bytes.to_vec();
    staged[5] = (staged[5] & 0xfc) | target.value();
    Ok(staged)
}

pub fn trigger_value(ap_data_byte1: u8) -> u8 {
    (ap_data_byte1 & 0xfc) | 0x01
}

pub fn sha256_hex(bytes: &[u8]) -> String {
    Sha256::digest(bytes)
        .iter()
        .map(|byte| format!("{byte:02X}"))
        .collect()
}

pub(crate) fn validate_writable_variable(variable: &FirmwareVariable) -> Result<()> {
    if variable.bytes.len() != EXPECTED_VARIABLE_LENGTH {
        bail!(
            "refusing write: expected a {EXPECTED_VARIABLE_LENGTH}-byte MsiDCVarData value, found {}",
            variable.bytes.len()
        );
    }
    if variable.attributes != EXPECTED_VARIABLE_ATTRIBUTES {
        bail!(
            "refusing write: expected UEFI attributes 0x{EXPECTED_VARIABLE_ATTRIBUTES:08X}, found 0x{:08X}",
            variable.attributes
        );
    }
    Ok(())
}

/// Optional ACPI diagnostics. Calls are serialized with firmware transactions.
pub fn collect_snapshot(allow_unknown_hardware: bool) -> Result<StateSnapshot> {
    collect_state(allow_unknown_hardware, true)
}

/// Pure status reads: never connects to or invokes the ACPI/debugfs transport.
pub fn collect_status() -> Result<StateSnapshot> {
    collect_state(true, false)
}

fn collect_state(allow_unknown_hardware: bool, diagnose_acpi: bool) -> Result<StateSnapshot> {
    use transaction::{DiskJournal, Journal, pending_state, validate_machine};
    let machine = query_machine()?;
    if !machine.expected_hardware && !allow_unknown_hardware {
        bail!(
            "unsupported hardware: model '{}', board '{}'",
            machine.model,
            machine.board
        );
    }
    let variable = read_msi_variable();
    let layout_error = variable
        .as_ref()
        .ok()
        .and_then(|v| validate_writable_variable(v).err())
        .map(|e| e.to_string());
    let firmware = AvailableSection::from_result(
        variable.and_then(|variable| FirmwareInfo::decode(&variable)),
    );
    let power = ac_power_online().unwrap_or(false);
    let record = DiskJournal::native().and_then(|journal| journal.load());
    let boot_id = platform::boot_id().unwrap_or_else(|_| "unknown-boot".into());
    let (pending, pending_shutdown) = match (&record, &firmware.value) {
        (Ok(record), Some(info)) => {
            let state = pending_state(record.as_ref(), info, &boot_id);
            (state.reason().map(str::to_owned), state.needs_shutdown())
        }
        (Err(error), _) => (
            Some(format!("transaction journal unavailable: {error:#}")),
            false,
        ),
        _ => (None, false),
    };
    let block = validate_machine(&machine)
        .err()
        .map(|e| {
            let code = if machine.model != EXPECTED_MODEL || machine.board != EXPECTED_BOARD {
                "unsupported_hardware"
            } else {
                "unvalidated_bios"
            };
            (code, e.to_string())
        })
        .or_else(|| layout_error.map(|e| ("invalid_firmware", e)))
        .or_else(|| firmware.error.clone().map(|e| ("firmware_unavailable", e)))
        .or_else(|| {
            firmware
                .value
                .as_ref()
                .filter(|f| !f.new_switch_supported)
                .map(|_| {
                    (
                        "unsupported_interface",
                        "firmware does not advertise the characterized switch".into(),
                    )
                })
        })
        .or_else(|| {
            pending.clone().map(|e| {
                (
                    if pending_shutdown {
                        "pending_shutdown"
                    } else {
                        "recovery_required"
                    },
                    e,
                )
            })
        })
        .or_else(|| {
            (!power).then(|| {
                (
                    "ac_power_required",
                    "AC power is required for switching".into(),
                )
            })
        })
        .or_else(|| {
            platform::acpi_available().err().map(|e| {
                (
                    "acpi_unavailable",
                    format!("MSI ACPI transport unavailable: {e:#}"),
                )
            })
        });
    let wmi_ap = if diagnose_acpi {
        AvailableSection::from_result(MsiAcpi::connect().and_then(|wmi| wmi.get_ap()))
    } else {
        AvailableSection::from_result(Err(anyhow::anyhow!(
            "not probed: --status never invokes ACPI; use --debug for an explicit diagnostic"
        )))
    };
    Ok(StateSnapshot {
        schema_version: 1,
        captured_at: Local::now().to_rfc3339(),
        elevated: is_elevated().unwrap_or(false),
        machine,
        secure_boot: AvailableSection::from_result(secure_boot_enabled()),
        registry: platform::query_registry_state(),
        firmware,
        wmi_ap,
        display_devices: query_display_devices()?,
        ac_power_online: power,
        internal_displays: platform::query_internal_displays()?,
        switching_supported: block.is_none(),
        switching_block_reason: block.as_ref().map(|(_, reason)| reason.clone()),
        switching_block_code: block.map(|(code, _)| code.into()),
        pending_shutdown,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn baseline() -> FirmwareVariable {
        let mut bytes = vec![0u8; EXPECTED_VARIABLE_LENGTH];
        bytes[5] = 0x3a;
        FirmwareVariable {
            bytes,
            attributes: EXPECTED_VARIABLE_ATTRIBUTES,
        }
    }

    #[test]
    fn decodes_observed_integrated_baseline() {
        let info = FirmwareInfo::decode(&baseline()).unwrap();
        assert_eq!(info.selected_target_mode, Some(Mode::Integrated));
        assert_eq!(info.current_mode, Some(Mode::Integrated));
        assert_eq!(info.available_modes(), Mode::ALL);
    }

    #[test]
    fn stages_only_target_bits() {
        let before = baseline();
        for (mode, expected) in [
            (Mode::MsHybrid, 0x38),
            (Mode::Discrete, 0x39),
            (Mode::Integrated, 0x3a),
        ] {
            let after = stage_target(&before.bytes, mode).unwrap();
            assert_eq!(after[5], expected);
            assert_eq!(&after[..5], &before.bytes[..5]);
            assert_eq!(&after[6..], &before.bytes[6..]);
        }
    }

    #[test]
    fn trigger_preserves_upper_six_bits() {
        for value in 0u8..=u8::MAX {
            let trigger = trigger_value(value);
            assert_eq!(trigger & 0xfc, value & 0xfc);
            assert_eq!(trigger & 0x03, 0x01);
        }
    }

    #[test]
    fn writable_variable_requires_exact_layout_and_attributes() {
        let mut variable = baseline();
        variable.bytes.pop();
        assert!(validate_writable_variable(&variable).is_err());

        let mut variable = baseline();
        variable.attributes ^= 1;
        assert!(validate_writable_variable(&variable).is_err());
    }

    #[test]
    fn decodes_get_ap_success_and_ready_flag() {
        let state = decode_ap_state(&[1, 0xaa, 0x82]).unwrap();
        assert_eq!(state.flag, 1);
        assert_eq!(state.data_byte1, 0x82);
        assert!(state.apply_ready);
    }

    #[test]
    fn rejects_unsuccessful_or_short_get_ap_package() {
        assert!(decode_ap_state(&[0, 0, 0]).is_err());
        assert!(decode_ap_state(&[1, 0]).is_err());
    }
}
