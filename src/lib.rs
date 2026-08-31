#![cfg_attr(not(windows), allow(dead_code))]

#[cfg(not(windows))]
compile_error!("msi-gpu-mux supports Windows only");

use std::collections::BTreeMap;
use std::ffi::c_void;
use std::fmt;
use std::mem::size_of;
use std::path::PathBuf;
use std::str::FromStr;

use anyhow::{Context, Result, anyhow, bail};
use chrono::Local;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use windows::Win32::Foundation::{
    CloseHandle, ERROR_NOT_ALL_ASSIGNED, ERROR_SUCCESS, GetLastError, HANDLE, LUID, SetLastError,
};
use windows::Win32::Security::{
    AdjustTokenPrivileges, GetTokenInformation, LUID_AND_ATTRIBUTES, LookupPrivilegeValueW,
    SE_PRIVILEGE_ENABLED, TOKEN_ADJUST_PRIVILEGES, TOKEN_ELEVATION, TOKEN_PRIVILEGES, TOKEN_QUERY,
    TokenElevation,
};
use windows::Win32::System::Threading::{GetCurrentProcess, OpenProcessToken};
use windows::Win32::System::WindowsProgramming::{
    GetFirmwareEnvironmentVariableExW, SetFirmwareEnvironmentVariableExW,
};
use windows::core::{HSTRING, PCWSTR, w};
use winreg::RegKey;
use winreg::enums::HKEY_LOCAL_MACHINE;
use wmi::{Variant, WMIConnection};

pub const EXPECTED_MODEL: &str = "Vector 16 HX AI A2XWIG";
pub const EXPECTED_BOARD: &str = "MS-15M3";
pub const EXPECTED_BIOS: &str = "E15M3IMS.116";
pub const MSI_VARIABLE_NAME: &str = "MsiDCVarData";
pub const MSI_VARIABLE_GUID: &str = "{DD96BAAF-145E-4F56-B1CF-193256298E99}";
pub const EXPECTED_VARIABLE_LENGTH: usize = 20;
pub const EXPECTED_VARIABLE_ATTRIBUTES: u32 = 0x0000_0007;

const SECURE_BOOT_VARIABLE_NAME: &str = "SecureBoot";
const EFI_GLOBAL_VARIABLE_GUID: &str = "{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}";
const GENERAL_SETTING_KEY: &str =
    r"SOFTWARE\WOW6432Node\MSI\MSI Center\Component\Base Module\GeneralSetting";

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
}

#[allow(non_snake_case)]
#[derive(Deserialize)]
struct ComputerRow {
    Manufacturer: String,
    Model: String,
}

#[allow(non_snake_case)]
#[derive(Deserialize)]
struct BoardRow {
    Product: String,
    Version: String,
}

#[allow(non_snake_case)]
#[derive(Deserialize)]
struct BiosRow {
    SMBIOSBIOSVersion: String,
}

#[allow(non_snake_case)]
#[derive(Deserialize)]
struct DisplayRow {
    Name: String,
    Status: Option<String>,
    ConfigManagerErrorCode: Option<u32>,
    Present: Option<bool>,
    HardwareID: Option<Vec<String>>,
}

#[allow(non_snake_case)]
#[derive(Deserialize)]
struct BatteryStatusRow {
    PowerOnline: bool,
}

#[allow(non_snake_case)]
#[derive(Deserialize)]
struct MsiAcpiRow {
    __Path: String,
    InstanceName: String,
}

struct OwnedHandle(HANDLE);

impl Drop for OwnedHandle {
    fn drop(&mut self) {
        if !self.0.is_invalid() {
            let _ = unsafe { CloseHandle(self.0) };
        }
    }
}

fn open_process_token(access: windows::Win32::Security::TOKEN_ACCESS_MASK) -> Result<OwnedHandle> {
    let mut token = HANDLE::default();
    unsafe { OpenProcessToken(GetCurrentProcess(), access, &mut token) }
        .context("OpenProcessToken failed")?;
    Ok(OwnedHandle(token))
}

pub fn is_elevated() -> Result<bool> {
    let token = open_process_token(TOKEN_QUERY)?;
    let mut elevation = TOKEN_ELEVATION::default();
    let mut returned = 0u32;
    unsafe {
        GetTokenInformation(
            token.0,
            TokenElevation,
            Some((&mut elevation as *mut TOKEN_ELEVATION).cast::<c_void>()),
            size_of::<TOKEN_ELEVATION>() as u32,
            &mut returned,
        )
    }
    .context("GetTokenInformation(TokenElevation) failed")?;
    Ok(elevation.TokenIsElevated != 0)
}

fn enable_system_environment_privilege() -> Result<()> {
    let token = open_process_token(TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES)?;
    let mut luid = LUID::default();
    unsafe {
        LookupPrivilegeValueW(
            PCWSTR::null(),
            w!("SeSystemEnvironmentPrivilege"),
            &mut luid,
        )
    }
    .context("LookupPrivilegeValueW(SeSystemEnvironmentPrivilege) failed")?;

    let privileges = TOKEN_PRIVILEGES {
        PrivilegeCount: 1,
        Privileges: [LUID_AND_ATTRIBUTES {
            Luid: luid,
            Attributes: SE_PRIVILEGE_ENABLED,
        }],
    };

    unsafe {
        SetLastError(ERROR_SUCCESS);
        AdjustTokenPrivileges(token.0, false, Some(&privileges), 0, None, None)
    }
    .context("AdjustTokenPrivileges failed")?;

    let status = unsafe { GetLastError() };
    if status == ERROR_NOT_ALL_ASSIGNED {
        bail!("SeSystemEnvironmentPrivilege is not present in this process token");
    }
    if status != ERROR_SUCCESS {
        bail!("AdjustTokenPrivileges returned Win32 error {}", status.0);
    }
    Ok(())
}

fn read_firmware_variable_named(name: &str, guid: &str) -> Result<FirmwareVariable> {
    enable_system_environment_privilege()?;
    let name = HSTRING::from(name);
    let guid = HSTRING::from(guid);
    let mut bytes = vec![0u8; 4096];
    let mut attributes = 0u32;
    let length = unsafe {
        GetFirmwareEnvironmentVariableExW(
            &name,
            &guid,
            Some(bytes.as_mut_ptr().cast::<c_void>()),
            bytes.len() as u32,
            Some(&mut attributes),
        )
    };
    if length == 0 {
        return Err(windows::core::Error::from_thread())
            .context("GetFirmwareEnvironmentVariableExW failed");
    }
    bytes.truncate(length as usize);
    Ok(FirmwareVariable { bytes, attributes })
}

pub fn read_msi_variable() -> Result<FirmwareVariable> {
    read_firmware_variable_named(MSI_VARIABLE_NAME, MSI_VARIABLE_GUID)
}

pub fn write_msi_variable(variable: &FirmwareVariable) -> Result<()> {
    if variable.bytes.is_empty() {
        bail!("refusing to write an empty UEFI value");
    }
    enable_system_environment_privilege()?;
    let name = HSTRING::from(MSI_VARIABLE_NAME);
    let guid = HSTRING::from(MSI_VARIABLE_GUID);
    unsafe {
        SetFirmwareEnvironmentVariableExW(
            &name,
            &guid,
            Some(variable.bytes.as_ptr().cast::<c_void>()),
            variable.bytes.len() as u32,
            variable.attributes,
        )
    }
    .context("SetFirmwareEnvironmentVariableExW(MsiDCVarData) failed")
}

pub fn secure_boot_enabled() -> Result<bool> {
    let variable =
        read_firmware_variable_named(SECURE_BOOT_VARIABLE_NAME, EFI_GLOBAL_VARIABLE_GUID)?;
    let value = variable
        .bytes
        .first()
        .context("SecureBoot variable is empty")?;
    Ok(*value != 0)
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

pub fn query_machine() -> Result<MachineInfo> {
    let connection = WMIConnection::new().context("connect to ROOT\\CIMV2")?;
    let computer: ComputerRow = connection
        .raw_query::<ComputerRow>("SELECT Manufacturer, Model FROM Win32_ComputerSystem")?
        .into_iter()
        .next()
        .context("Win32_ComputerSystem returned no rows")?;
    let board: BoardRow = connection
        .raw_query::<BoardRow>("SELECT Product, Version FROM Win32_BaseBoard")?
        .into_iter()
        .next()
        .context("Win32_BaseBoard returned no rows")?;
    let bios: BiosRow = connection
        .raw_query::<BiosRow>("SELECT SMBIOSBIOSVersion FROM Win32_BIOS")?
        .into_iter()
        .next()
        .context("Win32_BIOS returned no rows")?;
    let expected_hardware = computer.Model == EXPECTED_MODEL && board.Product == EXPECTED_BOARD;
    Ok(MachineInfo {
        manufacturer: computer.Manufacturer,
        model: computer.Model,
        board: board.Product,
        board_revision: board.Version,
        bios: bios.SMBIOSBIOSVersion,
        expected_hardware,
    })
}

pub fn query_display_devices() -> Result<Vec<DisplayDevice>> {
    let connection = WMIConnection::new().context("connect to ROOT\\CIMV2")?;
    let rows: Vec<DisplayRow> = connection.raw_query(
        "SELECT Name, Status, ConfigManagerErrorCode, Present, HardwareID \
         FROM Win32_PnPEntity WHERE PNPClass = 'Display'",
    )?;
    Ok(rows
        .into_iter()
        .map(|row| DisplayDevice {
            friendly_name: row.Name,
            status: row.Status,
            problem_code: row.ConfigManagerErrorCode,
            present: row.Present,
            hardware_ids: row.HardwareID.unwrap_or_default(),
        })
        .collect())
}

pub fn ac_power_online() -> Result<bool> {
    let connection = WMIConnection::with_namespace_path("ROOT\\WMI")?;
    let rows: Vec<BatteryStatusRow> =
        connection.raw_query("SELECT PowerOnline FROM BatteryStatus")?;
    Ok(rows.into_iter().any(|row| row.PowerOnline))
}

pub fn query_registry_state() -> BTreeMap<String, Option<u32>> {
    const NAMES: [&str; 7] = [
        "GPUswitchSP",
        "GPUswitchST",
        "GPUswitchCH",
        "GPUswitchUMA",
        "GPUswitchDiscrete",
        "GPU_Switch",
        "GPU_Switch_Support",
    ];
    let mut result = BTreeMap::new();
    let key = RegKey::predef(HKEY_LOCAL_MACHINE).open_subkey(GENERAL_SETTING_KEY);
    for name in NAMES {
        let value = key
            .as_ref()
            .ok()
            .and_then(|key| key.get_value::<u32, _>(name).ok());
        result.insert(name.to_string(), value);
    }
    result
}

pub struct MsiAcpi {
    connection: WMIConnection,
    path: String,
}

impl MsiAcpi {
    pub fn connect() -> Result<Self> {
        let connection =
            WMIConnection::with_namespace_path("ROOT\\WMI").context("connect to ROOT\\WMI")?;
        let rows: Vec<MsiAcpiRow> =
            connection.raw_query("SELECT __Path, InstanceName FROM MSI_ACPI")?;
        let expected_instance = r"ACPI\PNP0C14\0_0";
        let row = rows
            .into_iter()
            .find(|row| row.InstanceName.eq_ignore_ascii_case(expected_instance))
            .ok_or_else(|| anyhow!("MSI_ACPI instance '{expected_instance}' was not found"))?;
        Ok(Self {
            connection,
            path: row.__Path,
        })
    }

    fn invoke_package32(&self, method: &str, input_bytes: [u8; 32]) -> Result<Vec<u8>> {
        let package = self.connection.get_object("Package_32")?.spawn_instance()?;
        package.put_property("Bytes", input_bytes.to_vec())?;

        let input_signature = self
            .connection
            .get_object("MSI_ACPI")?
            .get_method(method)?
            .with_context(|| format!("MSI_ACPI method '{method}' has no input signature"))?;
        let input = input_signature.spawn_instance()?;
        input.put_property("Data", Variant::Object(package))?;

        let output = self
            .connection
            .exec_method(&self.path, method, Some(&input))?
            .with_context(|| format!("MSI_ACPI method '{method}' returned no output"))?;
        let embedded = match output.get_property("Data")? {
            Variant::Object(object) => object,
            other => bail!("MSI_ACPI method '{method}' returned unexpected Data: {other:?}"),
        };
        embedded
            .get_property("Bytes")?
            .try_into()
            .with_context(|| format!("decode MSI_ACPI method '{method}' output bytes"))
    }

    pub fn get_ap(&self) -> Result<ApState> {
        let raw = self.invoke_package32("Get_AP", [0u8; 32])?;
        if raw.len() < 3 || raw[0] == 0 {
            bail!("MSI_ACPI Get_AP returned an unsuccessful or short package");
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

    pub fn set_data(&self, address: u8, value: u8) -> Result<()> {
        let mut input = [0u8; 32];
        input[0] = address;
        input[1] = value;
        let raw = self.invoke_package32("Set_Data", input)?;
        if raw.first().copied().unwrap_or(0) == 0 {
            bail!("MSI_ACPI Set_Data(0x{address:02X}) reported failure");
        }
        Ok(())
    }
}

pub fn collect_snapshot(allow_unknown_hardware: bool) -> Result<StateSnapshot> {
    let machine = query_machine()?;
    if !machine.expected_hardware && !allow_unknown_hardware {
        bail!(
            "unsupported hardware: model '{}', board '{}'; use the explicit override only for a deliberate read-only capture",
            machine.model,
            machine.board
        );
    }

    let firmware = AvailableSection::from_result(
        read_msi_variable().and_then(|variable| FirmwareInfo::decode(&variable)),
    );
    let wmi_ap = AvailableSection::from_result(MsiAcpi::connect().and_then(|wmi| wmi.get_ap()));
    let secure_boot = AvailableSection::from_result(secure_boot_enabled());

    Ok(StateSnapshot {
        schema_version: 1,
        captured_at: Local::now().to_rfc3339(),
        elevated: is_elevated().unwrap_or(false),
        machine,
        secure_boot,
        registry: query_registry_state(),
        firmware,
        wmi_ap,
        display_devices: query_display_devices()?,
    })
}

pub fn backup_directory() -> Result<PathBuf> {
    let root = std::env::var_os("LOCALAPPDATA")
        .map(PathBuf::from)
        .unwrap_or(std::env::current_dir()?);
    Ok(root.join("msi-gpu-mux").join("backups"))
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
}
