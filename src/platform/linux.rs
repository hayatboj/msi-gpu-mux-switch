use std::collections::BTreeMap;
use std::io::{Read, Seek, SeekFrom, Write};
use std::os::fd::AsRawFd;
use std::os::unix::fs::MetadataExt;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result, anyhow, bail};

use super::{AcpiAccess, Platform};
use crate::{
    ApState, DisplayDevice, EFI_GLOBAL_VARIABLE_GUID, EXPECTED_BOARD, EXPECTED_MODEL,
    FirmwareVariable, MSI_VARIABLE_GUID, MSI_VARIABLE_NAME, MachineInfo, SECURE_BOOT_VARIABLE_NAME,
    decode_ap_state, validate_writable_variable,
};

const MSI_WMI_DEVICE_NAME: &str = "ABBC0F6E-8EA1-11D1-00A0-C90629100000-0";
const MSI_WMI_GUID: &str = "ABBC0F6E-8EA1-11D1-00A0-C90629100000";
const MSI_WMI_DRIVER_NAME: &str = "msi-wmi-platform";
const EXPECTED_LINUX_WMI_PARENT: &str = "PNP0C14:00";
const FS_IMMUTABLE_FL: nix::libc::c_long = 0x0000_0010;

nix::ioctl_read!(fs_ioc_getflags, b'f', 1, nix::libc::c_long);
nix::ioctl_write_ptr!(fs_ioc_setflags, b'f', 2, nix::libc::c_long);

pub struct LinuxPlatform;

pub struct LinuxAcpi {
    debugfs_directory: PathBuf,
}

impl Platform for LinuxPlatform {
    type Acpi = LinuxAcpi;

    fn is_elevated() -> Result<bool> {
        let status =
            std::fs::read_to_string("/proc/self/status").context("read /proc/self/status")?;
        let uid_line = status
            .lines()
            .find(|line| line.starts_with("Uid:"))
            .context("/proc/self/status has no Uid field")?;
        let effective_uid = uid_line
            .split_whitespace()
            .nth(2)
            .context("/proc/self/status Uid field has no effective UID")?
            .parse::<u32>()
            .context("parse effective UID")?;
        Ok(effective_uid == 0)
    }

    fn read_msi_variable() -> Result<FirmwareVariable> {
        read_firmware_variable_named(MSI_VARIABLE_NAME, MSI_VARIABLE_GUID)
    }

    fn write_msi_variable(variable: &FirmwareVariable) -> Result<()> {
        let encoded = encode_efivarfs_file(variable)?;
        let path = efivar_path(MSI_VARIABLE_NAME, MSI_VARIABLE_GUID);
        let mut immutable = ImmutableFlagGuard::clear(&path)?;
        let write_result = write_efivar_once(&path, &encoded);
        let restore_result = immutable.restore();
        match (write_result, restore_result) {
            (Ok(()), Ok(())) => Ok(()),
            (Err(write_error), Ok(())) => Err(write_error),
            (Ok(()), Err(restore_error)) => Err(restore_error),
            (Err(write_error), Err(restore_error)) => Err(write_error.context(format!(
                "restoring the original efivarfs inode flags also failed: {restore_error:#}"
            ))),
        }
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
        let root = Path::new("/sys/class/dmi/id");
        let manufacturer = read_trimmed(root.join("sys_vendor"))?;
        let model = read_trimmed(root.join("product_name"))?;
        let board = read_trimmed(root.join("board_name"))?;
        let board_revision = read_trimmed(root.join("board_version"))?;
        let bios = read_trimmed(root.join("bios_version"))?;
        let expected_hardware = model == EXPECTED_MODEL && board == EXPECTED_BOARD;
        Ok(MachineInfo {
            manufacturer,
            model,
            board,
            board_revision,
            bios,
            expected_hardware,
        })
    }

    fn query_display_devices() -> Result<Vec<DisplayDevice>> {
        let mut devices = Vec::new();
        for entry in std::fs::read_dir("/sys/bus/pci/devices").context("read PCI device list")? {
            let entry = entry.context("read PCI device entry")?;
            let path = entry.path();
            let class = match read_trimmed(path.join("class")) {
                Ok(class) => class,
                Err(_) => continue,
            };
            if !class.to_ascii_lowercase().starts_with("0x03") {
                continue;
            }

            let address = entry.file_name().to_string_lossy().into_owned();
            let vendor = read_trimmed(path.join("vendor"))?;
            let device = read_trimmed(path.join("device"))?;
            let modalias = read_trimmed(path.join("modalias")).ok();
            let driver_bound = path.join("driver").exists();
            let mut hardware_ids = vec![format!("pci:{vendor}:{device}")];
            if let Some(modalias) = modalias {
                hardware_ids.push(modalias);
            }
            devices.push(DisplayDevice {
                friendly_name: format!("PCI display controller {vendor}:{device} at {address}"),
                status: Some(
                    if driver_bound {
                        "driver-bound"
                    } else {
                        "unbound"
                    }
                    .to_string(),
                ),
                problem_code: None,
                present: Some(true),
                hardware_ids,
            });
        }
        devices.sort_by(|left, right| left.friendly_name.cmp(&right.friendly_name));
        Ok(devices)
    }

    fn ac_power_online() -> Result<bool> {
        for entry in
            std::fs::read_dir("/sys/class/power_supply").context("read power-supply list")?
        {
            let path = entry.context("read power-supply entry")?.path();
            if matches!(read_trimmed(path.join("type")).as_deref(), Ok("Mains"))
                && matches!(read_trimmed(path.join("online")).as_deref(), Ok("1"))
            {
                return Ok(true);
            }
        }
        Ok(false)
    }

    fn query_registry_state() -> BTreeMap<String, Option<u32>> {
        BTreeMap::new()
    }

    fn backup_directory() -> Result<PathBuf> {
        let root = std::env::var_os("XDG_STATE_HOME")
            .map(PathBuf::from)
            .or_else(|| {
                std::env::var_os("HOME")
                    .map(|home| PathBuf::from(home).join(".local").join("state"))
            })
            .unwrap_or(std::env::current_dir()?);
        Ok(root.join("msi-gpu-mux").join("backups"))
    }

    fn privilege_requirement() -> &'static str {
        "root privileges"
    }

    fn recovery_key_warning() -> &'static str {
        "confirm that any full-disk-encryption recovery key is available off-machine before continuing"
    }

    fn shutdown_command() -> &'static str {
        "systemctl poweroff"
    }

    fn verification_command() -> &'static str {
        "sudo msi-mux-switch --debug"
    }

    fn wait_for_keypress() {}
}

impl AcpiAccess for LinuxAcpi {
    fn connect() -> Result<Self> {
        let wmi_device = PathBuf::from("/sys/bus/wmi/devices").join(MSI_WMI_DEVICE_NAME);
        let canonical_device = std::fs::canonicalize(&wmi_device)
            .with_context(|| format!("resolve expected MSI WMI device {}", wmi_device.display()))?;
        if !canonical_device
            .components()
            .any(|component| component.as_os_str() == EXPECTED_LINUX_WMI_PARENT)
        {
            bail!(
                "expected MSI WMI device is not attached to {EXPECTED_LINUX_WMI_PARENT}: {}",
                canonical_device.display()
            );
        }
        let driver = std::fs::canonicalize(wmi_device.join("driver"))
            .context("resolve driver bound to the expected MSI WMI device")?;
        if driver.file_name().and_then(|name| name.to_str()) != Some(MSI_WMI_DRIVER_NAME) {
            bail!(
                "expected MSI WMI device is not bound to the {MSI_WMI_DRIVER_NAME} kernel driver"
            );
        }
        if read_trimmed(wmi_device.join("guid"))? != MSI_WMI_GUID
            || read_trimmed(wmi_device.join("object_id"))? != "AM"
            || read_trimmed(wmi_device.join("instance_count"))? != "1"
        {
            bail!("expected MSI WMI device metadata does not match the characterized interface");
        }

        let debugfs_directory = PathBuf::from("/sys/kernel/debug")
            .join(format!("{MSI_WMI_DRIVER_NAME}-{MSI_WMI_DEVICE_NAME}"));
        for method in ["get_ap", "set_data"] {
            let path = debugfs_directory.join(method);
            let metadata = std::fs::metadata(&path).with_context(|| {
                format!(
                    "inspect {}; ensure debugfs is mounted and the process is root",
                    path.display()
                )
            })?;
            if !metadata.is_file() || metadata.uid() != 0 || metadata.mode() & 0o777 != 0o600 {
                bail!(
                    "{} is not the expected root-owned mode-0600 debugfs method file",
                    path.display()
                );
            }
        }
        Ok(Self { debugfs_directory })
    }

    fn get_ap(&self) -> Result<ApState> {
        decode_ap_state(&self.invoke_package32("get_ap", [0u8; 32])?)
    }

    fn set_data(&self, address: u8, value: u8) -> Result<()> {
        let mut input = [0u8; 32];
        input[0] = address;
        input[1] = value;
        let raw = self.invoke_package32("set_data", input)?;
        if raw[0] == 0 {
            bail!("MSI ACPI Set_Data(0x{address:02X}) reported failure");
        }
        Ok(())
    }
}

impl LinuxAcpi {
    fn invoke_package32(&self, method: &str, input: [u8; 32]) -> Result<[u8; 32]> {
        let path = self.debugfs_directory.join(method);
        let mut file = std::fs::OpenOptions::new()
            .read(true)
            .write(true)
            .open(&path)
            .with_context(|| format!("open MSI WMI debugfs method {}", path.display()))?;
        let written = file
            .write(&input)
            .with_context(|| format!("invoke MSI WMI method '{method}'"))?;
        if written != input.len() {
            bail!(
                "MSI WMI method '{method}' accepted a partial input: {written} of {} bytes",
                input.len()
            );
        }
        file.seek(SeekFrom::Start(0))
            .with_context(|| format!("rewind MSI WMI method '{method}' output"))?;
        let mut output = Vec::with_capacity(32);
        file.read_to_end(&mut output)
            .with_context(|| format!("read MSI WMI method '{method}' output"))?;
        output.try_into().map_err(|output: Vec<u8>| {
            anyhow!(
                "MSI WMI method '{method}' returned {} bytes instead of 32",
                output.len()
            )
        })
    }
}

fn read_trimmed(path: impl AsRef<Path>) -> Result<String> {
    let path = path.as_ref();
    std::fs::read_to_string(path)
        .with_context(|| format!("read {}", path.display()))
        .map(|value| value.trim().to_string())
}

fn efivar_path(name: &str, guid: &str) -> PathBuf {
    let guid = guid.trim_matches(['{', '}']).to_ascii_lowercase();
    PathBuf::from("/sys/firmware/efi/efivars").join(format!("{name}-{guid}"))
}

fn read_firmware_variable_named(name: &str, guid: &str) -> Result<FirmwareVariable> {
    let path = efivar_path(name, guid);
    let file = std::fs::read(&path).with_context(|| format!("read {}", path.display()))?;
    decode_efivarfs_file(&file).with_context(|| format!("decode {}", path.display()))
}

fn decode_efivarfs_file(file: &[u8]) -> Result<FirmwareVariable> {
    let attributes = file
        .get(..4)
        .context("efivarfs value is missing its four-byte attributes header")?;
    Ok(FirmwareVariable {
        bytes: file[4..].to_vec(),
        attributes: u32::from_le_bytes(attributes.try_into().expect("slice length was checked")),
    })
}

fn encode_efivarfs_file(variable: &FirmwareVariable) -> Result<Vec<u8>> {
    validate_writable_variable(variable)?;
    let mut encoded = Vec::with_capacity(4 + variable.bytes.len());
    encoded.extend_from_slice(&variable.attributes.to_le_bytes());
    encoded.extend_from_slice(&variable.bytes);
    Ok(encoded)
}

fn write_efivar_once(path: &Path, encoded: &[u8]) -> Result<()> {
    let mut file = std::fs::OpenOptions::new()
        .write(true)
        .open(path)
        .with_context(|| format!("open {} for an exact firmware write", path.display()))?;
    let written = file
        .write(encoded)
        .with_context(|| format!("write {}", path.display()))?;
    if written != encoded.len() {
        bail!(
            "firmware write was partial: expected {} bytes, wrote {written}",
            encoded.len()
        );
    }
    Ok(())
}

struct ImmutableFlagGuard {
    file: std::fs::File,
    original_flags: nix::libc::c_long,
    restored: bool,
}

impl ImmutableFlagGuard {
    fn clear(path: &Path) -> Result<Self> {
        let file = std::fs::OpenOptions::new()
            .read(true)
            .open(path)
            .with_context(|| format!("open {} to inspect inode flags", path.display()))?;
        let mut original_flags = 0;
        unsafe { fs_ioc_getflags(file.as_raw_fd(), &mut original_flags) }
            .with_context(|| format!("read inode flags for {}", path.display()))?;
        if original_flags & FS_IMMUTABLE_FL != 0 {
            let writable_flags = original_flags & !FS_IMMUTABLE_FL;
            unsafe { fs_ioc_setflags(file.as_raw_fd(), &writable_flags) }.with_context(|| {
                format!("temporarily clear immutable flag on {}", path.display())
            })?;
        }
        Ok(Self {
            file,
            original_flags,
            restored: false,
        })
    }

    fn restore(&mut self) -> Result<()> {
        if !self.restored {
            unsafe { fs_ioc_setflags(self.file.as_raw_fd(), &self.original_flags) }
                .context("restore original efivarfs inode flags")?;
            self.restored = true;
        }
        Ok(())
    }
}

impl Drop for ImmutableFlagGuard {
    fn drop(&mut self) {
        let _ = self.restore();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{EXPECTED_VARIABLE_ATTRIBUTES, EXPECTED_VARIABLE_LENGTH};

    fn baseline() -> FirmwareVariable {
        let mut bytes = vec![0u8; EXPECTED_VARIABLE_LENGTH];
        bytes[5] = 0x3a;
        FirmwareVariable {
            bytes,
            attributes: EXPECTED_VARIABLE_ATTRIBUTES,
        }
    }

    #[test]
    fn decodes_efivarfs_attributes_and_payload() {
        let variable = decode_efivarfs_file(&[0x07, 0x00, 0x00, 0x00, 0xaa, 0xbb]).unwrap();
        assert_eq!(variable.attributes, EXPECTED_VARIABLE_ATTRIBUTES);
        assert_eq!(variable.bytes, [0xaa, 0xbb]);
    }

    #[test]
    fn rejects_short_efivarfs_value() {
        let error = decode_efivarfs_file(&[0x07, 0x00, 0x00]).unwrap_err();
        assert!(error.to_string().contains("four-byte attributes header"));
    }

    #[test]
    fn encodes_exact_efivarfs_write() {
        let variable = baseline();
        let encoded = encode_efivarfs_file(&variable).unwrap();
        assert_eq!(encoded.len(), 4 + EXPECTED_VARIABLE_LENGTH);
        assert_eq!(&encoded[..4], &EXPECTED_VARIABLE_ATTRIBUTES.to_le_bytes());
        assert_eq!(&encoded[4..], variable.bytes);
    }
}
