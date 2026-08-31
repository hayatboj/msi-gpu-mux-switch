use std::collections::BTreeMap;
use std::ffi::c_void;
use std::io::{self, Write};
use std::mem::size_of;
use std::path::PathBuf;

use anyhow::{Context, Result, anyhow, bail};
use serde::Deserialize;
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

use super::{AcpiAccess, Platform};
use crate::{
    ApState, DisplayDevice, EFI_GLOBAL_VARIABLE_GUID, EXPECTED_BOARD, EXPECTED_MODEL,
    FirmwareVariable, MSI_VARIABLE_GUID, MSI_VARIABLE_NAME, MachineInfo, SECURE_BOOT_VARIABLE_NAME,
    decode_ap_state, validate_writable_variable,
};

const GENERAL_SETTING_KEY: &str =
    r"SOFTWARE\WOW6432Node\MSI\MSI Center\Component\Base Module\GeneralSetting";

#[link(name = "msvcrt")]
unsafe extern "C" {
    fn _getch() -> i32;
}

pub struct WindowsPlatform;

pub struct WindowsAcpi {
    connection: WMIConnection,
    path: String,
}

impl Platform for WindowsPlatform {
    type Acpi = WindowsAcpi;

    fn is_elevated() -> Result<bool> {
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

    fn read_msi_variable() -> Result<FirmwareVariable> {
        read_firmware_variable_named(MSI_VARIABLE_NAME, MSI_VARIABLE_GUID)
    }

    fn write_msi_variable(variable: &FirmwareVariable) -> Result<()> {
        validate_writable_variable(variable)?;
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

    fn secure_boot_enabled() -> Result<bool> {
        let variable =
            read_firmware_variable_named(SECURE_BOOT_VARIABLE_NAME, EFI_GLOBAL_VARIABLE_GUID)?;
        let value = variable
            .bytes
            .first()
            .context("SecureBoot variable is empty")?;
        Ok(*value != 0)
    }

    fn query_machine() -> Result<MachineInfo> {
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

    fn query_display_devices() -> Result<Vec<DisplayDevice>> {
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

    fn ac_power_online() -> Result<bool> {
        let connection = WMIConnection::with_namespace_path("ROOT\\WMI")?;
        let rows: Vec<BatteryStatusRow> =
            connection.raw_query("SELECT PowerOnline FROM BatteryStatus")?;
        Ok(rows.into_iter().any(|row| row.PowerOnline))
    }

    fn query_registry_state() -> BTreeMap<String, Option<u32>> {
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

    fn backup_directory() -> Result<PathBuf> {
        let root = std::env::var_os("LOCALAPPDATA")
            .map(PathBuf::from)
            .unwrap_or(std::env::current_dir()?);
        Ok(root.join("msi-gpu-mux").join("backups"))
    }

    fn privilege_requirement() -> &'static str {
        "an elevated Administrator process"
    }

    fn recovery_key_warning() -> &'static str {
        "confirm that the BitLocker recovery key is available off-machine before continuing"
    }

    fn shutdown_command() -> &'static str {
        "shutdown /s /t 0"
    }

    fn verification_command() -> &'static str {
        "msi-mux-switch.exe --debug"
    }

    fn wait_for_keypress() {
        eprint!("Press any key to exit...");
        let _ = io::stderr().flush();
        unsafe {
            _getch();
        }
        eprintln!();
    }
}

impl AcpiAccess for WindowsAcpi {
    fn connect() -> Result<Self> {
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

    fn get_ap(&self) -> Result<ApState> {
        decode_ap_state(&self.invoke_package32("Get_AP", [0u8; 32])?)
    }

    fn set_data(&self, address: u8, value: u8) -> Result<()> {
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

impl WindowsAcpi {
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
