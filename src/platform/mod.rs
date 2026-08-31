use std::collections::BTreeMap;
use std::path::PathBuf;

use anyhow::Result;

use crate::{ApState, DisplayDevice, FirmwareVariable, MachineInfo};

pub trait AcpiAccess: Sized {
    fn connect() -> Result<Self>;
    fn get_ap(&self) -> Result<ApState>;
    fn set_data(&self, address: u8, value: u8) -> Result<()>;
}

pub trait Platform {
    type Acpi: AcpiAccess;

    fn is_elevated() -> Result<bool>;
    fn read_msi_variable() -> Result<FirmwareVariable>;
    fn write_msi_variable(variable: &FirmwareVariable) -> Result<()>;
    fn secure_boot_enabled() -> Result<bool>;
    fn query_machine() -> Result<MachineInfo>;
    fn query_display_devices() -> Result<Vec<DisplayDevice>>;
    fn ac_power_online() -> Result<bool>;
    fn query_registry_state() -> BTreeMap<String, Option<u32>>;
    fn backup_directory() -> Result<PathBuf>;
    fn privilege_requirement() -> &'static str;
    fn recovery_key_warning() -> &'static str;
    fn shutdown_command() -> &'static str;
    fn verification_command() -> &'static str;
    fn wait_for_keypress();
}

#[cfg(target_os = "linux")]
mod linux;
#[cfg(windows)]
mod windows;

#[cfg(target_os = "linux")]
pub use linux::LinuxPlatform as NativePlatform;
#[cfg(windows)]
pub use windows::WindowsPlatform as NativePlatform;

pub struct MsiAcpi(<NativePlatform as Platform>::Acpi);

impl MsiAcpi {
    pub fn connect() -> Result<Self> {
        <NativePlatform as Platform>::Acpi::connect().map(Self)
    }

    pub fn get_ap(&self) -> Result<ApState> {
        self.0.get_ap()
    }

    pub fn set_data(&self, address: u8, value: u8) -> Result<()> {
        self.0.set_data(address, value)
    }
}

pub fn is_elevated() -> Result<bool> {
    NativePlatform::is_elevated()
}

pub fn read_msi_variable() -> Result<FirmwareVariable> {
    NativePlatform::read_msi_variable()
}

pub fn write_msi_variable(variable: &FirmwareVariable) -> Result<()> {
    NativePlatform::write_msi_variable(variable)
}

pub fn secure_boot_enabled() -> Result<bool> {
    NativePlatform::secure_boot_enabled()
}

pub fn query_machine() -> Result<MachineInfo> {
    NativePlatform::query_machine()
}

pub fn query_display_devices() -> Result<Vec<DisplayDevice>> {
    NativePlatform::query_display_devices()
}

pub fn ac_power_online() -> Result<bool> {
    NativePlatform::ac_power_online()
}

pub fn query_registry_state() -> BTreeMap<String, Option<u32>> {
    NativePlatform::query_registry_state()
}

pub fn backup_directory() -> Result<PathBuf> {
    NativePlatform::backup_directory()
}

pub fn privilege_requirement() -> &'static str {
    NativePlatform::privilege_requirement()
}

pub fn recovery_key_warning() -> &'static str {
    NativePlatform::recovery_key_warning()
}

pub fn shutdown_command() -> &'static str {
    NativePlatform::shutdown_command()
}

pub fn verification_command() -> &'static str {
    NativePlatform::verification_command()
}

pub fn wait_for_keypress() {
    NativePlatform::wait_for_keypress();
}
