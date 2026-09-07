//! Fail-closed firmware transaction orchestration. Restoring a selected target is
//! never represented as undoing an ACPI operation or as a complete BIOS backup.
#[cfg(unix)]
use std::fs::File;
use std::fs::{self, OpenOptions};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
#[cfg(not(test))]
use std::thread;
#[cfg(not(test))]
use std::time::Duration;

use anyhow::{Context, Result, bail};
use chrono::Utc;
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};

use crate::platform::{AcpiAccess, Platform};
use crate::{
    EXPECTED_BIOS, EXPECTED_BOARD, EXPECTED_MODEL, FirmwareInfo, FirmwareVariable, MachineInfo,
    Mode, sha256_hex, stage_target, trigger_value, validate_writable_variable,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum Phase {
    Prepared,
    StageAttempted,
    Staged,
    TriggerAttempted,
    AcknowledgementAttempted,
    PendingShutdown,
    TargetRestored,
    Reconciled,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TransactionRecord {
    pub schema_version: u32,
    pub created_at: String,
    pub boot_id: String,
    pub model: String,
    pub board: String,
    pub bios: String,
    pub requested_target: Mode,
    pub previous_target: Mode,
    pub previous_current: Mode,
    pub variable_sha256: String,
    pub phase: Phase,
    /// Set durably BEFORE entering the trigger call, including failed calls.
    pub trigger_attempted: bool,
    pub target_restoration_verified: bool,
}

pub trait Journal {
    fn load(&self) -> Result<Option<TransactionRecord>>;
    fn save(&mut self, record: &TransactionRecord) -> Result<()>;
}

pub struct DiskJournal {
    path: PathBuf,
}

impl DiskJournal {
    pub fn native() -> Result<Self> {
        Ok(Self {
            path: crate::backup_directory()?.join("transaction.json"),
        })
    }
}

#[cfg(unix)]
fn secure_metadata(path: &Path, directory: bool) -> Result<()> {
    use std::os::unix::fs::MetadataExt;
    let metadata =
        fs::symlink_metadata(path).with_context(|| format!("inspect {}", path.display()))?;
    if metadata.file_type().is_symlink()
        || metadata.is_dir() != directory
        || metadata.uid() != 0
        || metadata.mode() & 0o022 != 0
    {
        bail!(
            "refusing unsafe transaction path {}; expected root ownership and no group/other write access",
            path.display()
        );
    }
    if !directory && (!metadata.is_file() || metadata.nlink() != 1) {
        bail!("transaction path must be a regular file with one link");
    }
    Ok(())
}

#[cfg(not(unix))]
fn secure_metadata(_path: &Path, _directory: bool) -> Result<()> {
    Ok(())
}

impl Journal for DiskJournal {
    fn load(&self) -> Result<Option<TransactionRecord>> {
        let parent = self
            .path
            .parent()
            .context("transaction directory missing")?;
        match fs::symlink_metadata(parent) {
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(None),
            Err(e) => return Err(e).context("inspect transaction directory"),
            Ok(_) => secure_metadata(parent, true)?,
        }
        match fs::symlink_metadata(&self.path) {
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(None),
            Err(e) => return Err(e).context("inspect transaction journal"),
            Ok(_) => secure_metadata(&self.path, false)?,
        }
        let mut options = OpenOptions::new();
        options.read(true);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.custom_flags(nix::libc::O_NOFOLLOW | nix::libc::O_CLOEXEC);
        }
        let mut data = Vec::new();
        options
            .open(&self.path)?
            .take(65_537)
            .read_to_end(&mut data)?;
        if data.len() > 65_536 {
            bail!("transaction journal is too large");
        }
        let record: TransactionRecord = serde_json::from_slice(&data)
            .context("invalid transaction journal; refusing another write")?;
        if record.schema_version != 1 {
            bail!("unknown transaction journal version");
        }
        Ok(Some(record))
    }

    fn save(&mut self, record: &TransactionRecord) -> Result<()> {
        let parent = self
            .path
            .parent()
            .context("transaction directory missing")?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::DirBuilderExt;
            // All ancestors are fixed system paths; no HOME/XDG/PKEXEC environment input.
            for ancestor in parent.ancestors().skip(1) {
                secure_metadata(ancestor, true)?;
            }
            match fs::DirBuilder::new().mode(0o755).create(parent) {
                Ok(()) => {
                    use std::os::unix::fs::PermissionsExt;
                    fs::set_permissions(parent, fs::Permissions::from_mode(0o755))?;
                }
                Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => (),
                Err(error) => return Err(error.into()),
            }
        }
        #[cfg(not(unix))]
        fs::create_dir_all(parent)?;
        secure_metadata(parent, true)?;
        if self.path.exists() {
            secure_metadata(&self.path, false)?;
        }
        let temporary = parent.join(format!(".transaction-{}.tmp", std::process::id()));
        let mut options = OpenOptions::new();
        options.write(true).create_new(true);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options
                .mode(0o644)
                .custom_flags(nix::libc::O_NOFOLLOW | nix::libc::O_CLOEXEC);
        }
        let mut file = options
            .open(&temporary)
            .context("create exclusive transaction journal temporary file")?;
        let result = (|| -> Result<()> {
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                file.set_permissions(fs::Permissions::from_mode(0o644))?;
            }
            file.write_all(&serde_json::to_vec_pretty(record)?)?;
            file.sync_all().context("persist transaction journal")?;
            #[cfg(windows)]
            {
                use windows::Win32::Storage::FileSystem::{
                    MOVEFILE_REPLACE_EXISTING, MOVEFILE_WRITE_THROUGH, MoveFileExW,
                };
                use windows::core::HSTRING;
                // Replace atomically; never remove the last durable journal first.
                unsafe {
                    MoveFileExW(
                        &HSTRING::from(temporary.as_os_str()),
                        &HSTRING::from(self.path.as_os_str()),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH,
                    )
                }
                .context("publish durable transaction journal")?;
            }
            #[cfg(not(windows))]
            fs::rename(&temporary, &self.path).context("publish transaction journal")?;
            #[cfg(unix)]
            File::open(parent)?
                .sync_all()
                .context("persist transaction journal directory")?;
            Ok(())
        })();
        if result.is_err() {
            let _ = fs::remove_file(&temporary);
        }
        result
    }
}

/// Every cooperating Linux process uses this lock for the entire lifetime of
/// an ACPI connection. Status polling deliberately never opens that connection.
#[cfg(target_os = "linux")]
pub struct OperationLock {
    _file: nix::fcntl::Flock<File>,
}
#[cfg(target_os = "linux")]
impl OperationLock {
    pub fn acquire() -> Result<Self> {
        use std::os::unix::fs::DirBuilderExt;
        use std::os::unix::fs::OpenOptionsExt;
        secure_metadata(Path::new("/run"), true)?;
        let parent = Path::new("/run/msi-mux");
        match fs::DirBuilder::new().mode(0o700).create(parent) {
            Ok(()) => (),
            Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => (),
            Err(error) => {
                return Err(error).context("create root MUX lock directory (requires root)");
            }
        }
        secure_metadata(parent, true)?;
        let path = parent.join("operation.lock");
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(false)
            .mode(0o600)
            .custom_flags(nix::libc::O_NOFOLLOW | nix::libc::O_CLOEXEC)
            .open(&path)
            .context("open root MUX operation lock (requires root)")?;
        secure_metadata(&path, false)?;
        Self::lock_file(file)
    }

    fn lock_file(file: File) -> Result<Self> {
        use nix::fcntl::{Flock, FlockArg};
        let file = Flock::lock(file, FlockArg::LockExclusiveNonblock).map_err(|(_, error)| {
            anyhow::anyhow!("another MUX diagnostic or transition is running: {error}")
        })?;
        Ok(Self { _file: file })
    }
}

/// SIGINT/SIGTERM/SIGHUP cannot interrupt the bounded critical section. SIGKILL
/// or power loss still leave the durable attempted phase for fail-closed recovery.
pub struct CriticalOperationGuard {
    #[cfg(target_os = "linux")]
    handlers: Vec<(nix::sys::signal::Signal, nix::sys::signal::SigHandler)>,
}
impl CriticalOperationGuard {
    pub fn enter() -> Result<Self> {
        #[cfg(all(target_os = "linux", not(test)))]
        let mut guard = Self {
            handlers: Vec::new(),
        };
        #[cfg(all(target_os = "linux", test))]
        let guard = Self {
            handlers: Vec::new(),
        };
        #[cfg(not(target_os = "linux"))]
        let guard = Self {};
        #[cfg(all(target_os = "linux", not(test)))]
        {
            use nix::sys::signal::{SigHandler, Signal, signal};
            for sig in [Signal::SIGINT, Signal::SIGTERM, Signal::SIGHUP] {
                // Single-threaded CLI; SIG_IGN uses no callback or shared state.
                let old = unsafe { signal(sig, SigHandler::SigIgn) }?;
                guard.handlers.push((sig, old));
            }
        }
        Ok(guard)
    }
}
impl Drop for CriticalOperationGuard {
    fn drop(&mut self) {
        #[cfg(target_os = "linux")]
        for (sig, old) in self.handlers.drain(..).rev() {
            // Restores exactly the handlers installed before this guard.
            let _ = unsafe { nix::sys::signal::signal(sig, old) };
        }
    }
}

pub fn validate_machine(machine: &MachineInfo) -> Result<()> {
    if machine.model != EXPECTED_MODEL
        || machine.board != EXPECTED_BOARD
        || !machine.expected_hardware
    {
        bail!(
            "unsupported hardware: model '{}', board '{}'; writes require exact characterized hardware",
            machine.model,
            machine.board
        );
    }
    if machine.bios != EXPECTED_BIOS {
        bail!(
            "BIOS '{}' is not the characterized '{}'; switching is blocked",
            machine.bios,
            EXPECTED_BIOS
        );
    }
    Ok(())
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PendingState {
    Clear,
    ShutdownRequired(String),
    RecoveryRequired(String),
}

impl PendingState {
    pub fn reason(&self) -> Option<&str> {
        match self {
            Self::Clear => None,
            Self::ShutdownRequired(reason) | Self::RecoveryRequired(reason) => Some(reason),
        }
    }

    pub fn needs_shutdown(&self) -> bool {
        matches!(self, Self::ShutdownRequired(_))
    }
}

/// One classification supplies both the write gate and the desktop indicator.
/// An unresolved foreign journal or unknown boot identity must never be presented
/// as a routine shutdown request, even when target and current modes differ.
pub fn pending_state(
    record: Option<&TransactionRecord>,
    info: &FirmwareInfo,
    boot_id: &str,
) -> PendingState {
    use PendingState::{Clear, RecoveryRequired, ShutdownRequired};
    let unresolved = record.filter(|record| record.phase != Phase::Reconciled);
    if let Some(record) = unresolved {
        if record.model != EXPECTED_MODEL
            || record.board != EXPECTED_BOARD
            || record.bios != EXPECTED_BIOS
        {
            return RecoveryRequired(
                "unresolved transaction belongs to different hardware or BIOS".into(),
            );
        }
        if record.boot_id.trim().is_empty() || record.boot_id == "unknown-boot" {
            return RecoveryRequired("unresolved transaction has no trustworthy boot identity; manual recovery is required".into());
        }
    }
    if boot_id.trim().is_empty() || boot_id == "unknown-boot" {
        return RecoveryRequired(
            "current boot identity is unavailable; transaction reconciliation cannot be verified"
                .into(),
        );
    }
    if info.current_mode.is_none() || info.selected_target_mode.is_none() {
        return RecoveryRequired("firmware mode bits are not recognized".into());
    }
    if info.current_mode != info.selected_target_mode {
        return ShutdownRequired("selected target differs from the current mode; save work and perform a full manual shutdown before another transition".into());
    }
    if let Some(record) = unresolved {
        if record.phase == Phase::Prepared
            && info.current_mode == Some(record.previous_current)
            && record.previous_current == record.previous_target
        {
            // The attempted-write phase is always synced before a write call.
            // A record still at Prepared therefore proves no write was started.
            return Clear;
        }
        if record.boot_id == boot_id {
            return ShutdownRequired("an unresolved transaction exists from this boot; save work and perform a full manual shutdown before another transition".into());
        }
        // On a new boot accept only the requested mode, or a verified original
        // target/current pair. An arbitrary matching pair is not reconciliation.
        let current = info.current_mode;
        if current != Some(record.requested_target)
            && !(record.target_restoration_verified
                && current == Some(record.previous_target)
                && record.previous_current == record.previous_target)
            && !(record.phase == Phase::Prepared && current == Some(record.previous_current))
        {
            return RecoveryRequired(
                "previous transaction has not reconciled after boot; manual recovery is required"
                    .into(),
            );
        }
    }
    Clear
}

pub fn pending_reason(
    record: Option<&TransactionRecord>,
    info: &FirmwareInfo,
    boot_id: &str,
) -> Option<String> {
    pending_state(record, info, boot_id)
        .reason()
        .map(str::to_owned)
}

pub fn apply<P: Platform, J: Journal>(
    target: Mode,
    journal: &mut J,
    mut event: impl FnMut(&str, &str, Value),
) -> Result<()> {
    validate_machine(&P::query_machine()?)?;
    if !P::is_elevated()? {
        bail!("switching requires {}", P::privilege_requirement());
    }
    if !P::ac_power_online()? {
        bail!("AC power is required for a GPU mode transition");
    }
    let acpi = P::Acpi::connect()?; // Linux lock held through restoration/final journal sync.
    let boot_id = P::boot_id()?;
    let original_record = journal.load()?;
    let before = P::read_msi_variable()?;
    validate_writable_variable(&before)?;
    let info = FirmwareInfo::decode(&before)?;
    if let Some(reason) = pending_reason(original_record.as_ref(), &info, &boot_id) {
        bail!("{reason}");
    }
    if !info.available_modes().contains(&target) {
        bail!("requested mode is not advertised by the characterized firmware");
    }
    if info.current_mode == Some(target) {
        bail!("requested mode is already active; no state was changed");
    }
    let ap = acpi.get_ap()?;
    if ap.apply_ready {
        bail!("apply-ready is already asserted; refusing an ambiguous transition");
    }
    event(
        "preflight",
        "Apply-ready is clear before staging.",
        json!({"data_byte1":ap.data_byte1,"apply_ready":false}),
    );
    // No overrides: revalidate hardware, power, complete variable, capability and
    // selected/current mode after ACPI preflight immediately before staging.
    let machine = P::query_machine()?;
    validate_machine(&machine)?;
    if !P::ac_power_online()? {
        bail!("AC power disappeared before firmware write");
    }
    let latest = P::read_msi_variable()?;
    validate_writable_variable(&latest)?;
    if latest.bytes != before.bytes || latest.attributes != before.attributes {
        bail!("firmware state changed during preflight; no state was changed");
    }
    let previous_target = info
        .selected_target_mode
        .context("unknown selected target")?;
    let staged = FirmwareVariable {
        bytes: stage_target(&before.bytes, target)?,
        attributes: before.attributes,
    };
    let mut record = TransactionRecord {
        schema_version: 1,
        created_at: Utc::now().to_rfc3339(),
        boot_id,
        model: machine.model,
        board: machine.board,
        bios: machine.bios,
        requested_target: target,
        previous_target,
        previous_current: info.current_mode.context("unknown current mode")?,
        variable_sha256: sha256_hex(&before.bytes),
        phase: Phase::Prepared,
        trigger_attempted: false,
        target_restoration_verified: false,
    };
    journal
        .save(&record)
        .context("save transaction metadata before firmware write")?;
    event(
        "rollback_record",
        "Durable target-restoration metadata saved; this is not a complete BIOS backup.",
        json!({"phase": record.phase}),
    );
    let _critical = CriticalOperationGuard::enter()?;
    event(
        "critical_section",
        "Transition in progress. Do not power off; normal termination signals are ignored while the critical operation completes.",
        Value::Null,
    );
    record.phase = Phase::StageAttempted;
    journal.save(&record)?;
    let mut write_attempted = false;
    let result = (|| -> Result<()> {
        // Journaling can take time. Recheck after its durable sync and directly
        // before the first firmware mutation, under the same operation lock.
        validate_machine(&P::query_machine()?)?;
        if !P::ac_power_online()? {
            bail!("AC power disappeared immediately before staging");
        }
        verify::<P>(&before).context("firmware changed while journaling; refusing staging")?;
        write_attempted = true;
        P::write_msi_variable(&staged)
            .context("stage target (write completion may be ambiguous)")?;
        verify::<P>(&staged).context("verify staged target")?;
        record.phase = Phase::Staged;
        journal.save(&record)?;
        event(
            "target_staged",
            "The complete firmware target and attributes were verified.",
            json!({"mode": target}),
        );
        let ap = acpi.get_ap()?;
        if ap.apply_ready {
            bail!("apply-ready became asserted before trigger; refusing ambiguous transition");
        }
        validate_machine(&P::query_machine()?)?;
        if !P::ac_power_online()? {
            bail!("AC power disappeared before the trigger");
        }
        verify::<P>(&staged).context("staged target changed before the trigger")?;
        let trigger = trigger_value(ap.data_byte1);
        record.trigger_attempted = true;
        record.phase = Phase::TriggerAttempted;
        journal
            .save(&record)
            .context("persist trigger attempt before ACPI call")?;
        event(
            "trigger",
            "Attempting the characterized firmware trigger.",
            json!({"address": 0xd1, "value": trigger}),
        );
        acpi.set_data(0xd1, trigger)
            .context("trigger call failed; hardware may still have accepted the request")?;
        let mut ready = false;
        for _ in 0..20 {
            #[cfg(not(test))]
            thread::sleep(Duration::from_millis(250));
            if acpi.get_ap()?.apply_ready {
                ready = true;
                break;
            }
        }
        if !ready {
            bail!("firmware did not assert apply-ready within five seconds");
        }
        event(
            "apply_ready",
            "Firmware apply-ready asserted.",
            json!({"apply_ready":true}),
        );
        record.phase = Phase::AcknowledgementAttempted;
        journal.save(&record)?;
        event(
            "acknowledgement",
            "Attempting the characterized acknowledgement.",
            json!({"address":0xbe,"value":2}),
        );
        acpi.set_data(0xbe, 2)
            .context("acknowledgement completion may be ambiguous")?;
        record.phase = Phase::PendingShutdown;
        journal.save(&record)?;
        Ok(())
    })();
    if let Err(error) = result {
        if !write_attempted {
            record.phase = Phase::Prepared;
            return match journal.save(&record) {
                Ok(()) => Err(error.context("no firmware write or trigger was attempted")),
                Err(persistence_error) => Err(error.context(format!("no firmware write was attempted; journal reconciliation failed: {persistence_error:#}"))),
            };
        }
        let restore = restore_previous_target::<P>(&before, previous_target);
        match restore {
            Ok(()) => {
                record.target_restoration_verified = true;
                record.phase = Phase::TargetRestored;
                event(
                    "target_restored",
                    "Previous selected target restored and verified. This does not undo a possibly accepted ACPI operation.",
                    json!({"mode":previous_target}),
                );
            }
            Err(ref restore_error) => event(
                "rollback_failed",
                &format!("Previous-target restoration could not be verified: {restore_error:#}"),
                Value::Null,
            ),
        }
        let persistence = journal.save(&record);
        if record.trigger_attempted {
            event(
                "trigger_sent_before_failure",
                "The trigger was attempted before failure; its hardware effect is uncertain. Save work and perform a full manual shutdown. Do not retry in this boot.",
                json!({"trigger_attempted":true}),
            );
        }
        let mut message = format!(
            "{error:#}; firmware state may have changed; another transition is blocked until shutdown and reconciliation"
        );
        if let Err(e) = restore {
            message.push_str(&format!("; target restoration failed: {e:#}"));
        }
        if let Err(e) = persistence {
            message.push_str(&format!("; final journal save failed: {e:#}"));
        }
        bail!("{message}");
    }
    Ok(())
}

fn verify<P: Platform>(expected: &FirmwareVariable) -> Result<()> {
    let actual = P::read_msi_variable()?;
    if actual.bytes != expected.bytes || actual.attributes != expected.attributes {
        bail!("full firmware value or attributes do not match");
    }
    Ok(())
}

fn restore_previous_target<P: Platform>(before: &FirmwareVariable, previous: Mode) -> Result<()> {
    validate_machine(&P::query_machine()?)?;
    let live = P::read_msi_variable()?;
    validate_writable_variable(&live)?;
    // Other firmware data changing underneath us is not safe to overwrite.
    if stage_target(&live.bytes, previous)? != before.bytes || live.attributes != before.attributes
    {
        bail!("non-target firmware state changed; refusing restoration write");
    }
    if live.bytes == before.bytes && live.attributes == before.attributes {
        return Ok(()); // Already verified; avoid an unnecessary UEFI write.
    }
    if !P::ac_power_online()? {
        bail!("AC power unavailable for target restoration");
    }
    let restored = FirmwareVariable {
        bytes: stage_target(&live.bytes, previous)?,
        attributes: live.attributes,
    };
    P::write_msi_variable(&restored)?;
    verify::<P>(&restored)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{ApState, DisplayDevice, EXPECTED_VARIABLE_ATTRIBUTES, EXPECTED_VARIABLE_LENGTH};
    use std::cell::RefCell;
    use std::collections::BTreeMap;

    #[derive(Clone, Copy, PartialEq, Eq)]
    enum Failure {
        None,
        BeforeWrite,
        AfterWrite,
        StageRead,
        JournalStale,
        TriggerReply,
        TriggerRead,
        AckReply,
        Busy,
        Stale,
        PowerLost,
        MachineChanged,
        OtherByteChanged,
    }
    struct FakeState {
        variable: FirmwareVariable,
        failure: Failure,
        reads: usize,
        writes: usize,
        queries: usize,
        power_queries: usize,
        ap_queries: usize,
        trigger_attempts: usize,
        ack_attempts: usize,
        apply_effect: bool,
        persisted_phase: Option<Phase>,
    }
    thread_local! {
        static STATE: RefCell<FakeState> = RefCell::new(fresh(Failure::None));
    }
    fn fresh(failure: Failure) -> FakeState {
        let mut bytes = vec![0xa5; EXPECTED_VARIABLE_LENGTH];
        bytes[5] = 0x30;
        FakeState {
            variable: FirmwareVariable {
                bytes,
                attributes: EXPECTED_VARIABLE_ATTRIBUTES,
            },
            failure,
            reads: 0,
            writes: 0,
            queries: 0,
            power_queries: 0,
            ap_queries: 0,
            trigger_attempts: 0,
            ack_attempts: 0,
            apply_effect: false,
            persisted_phase: None,
        }
    }
    fn machine() -> MachineInfo {
        MachineInfo {
            manufacturer: "MSI".into(),
            model: EXPECTED_MODEL.into(),
            board: EXPECTED_BOARD.into(),
            board_revision: "1".into(),
            bios: EXPECTED_BIOS.into(),
            expected_hardware: true,
        }
    }
    struct Fake;
    struct FakeAcpi;
    impl AcpiAccess for FakeAcpi {
        fn connect() -> Result<Self> {
            STATE.with(|s| {
                if s.borrow().failure == Failure::Busy {
                    bail!("operation lock busy");
                }
                Ok(Self)
            })
        }
        fn get_ap(&self) -> Result<ApState> {
            STATE.with(|state| {
                let mut s = state.borrow_mut();
                s.ap_queries += 1;
                if s.failure == Failure::TriggerRead && s.apply_effect {
                    bail!("trigger applied but output read failed");
                }
                if s.failure == Failure::OtherByteChanged && s.ap_queries == 2 {
                    s.variable.bytes[9] ^= 1;
                    bail!("external firmware state change");
                }
                Ok(ApState {
                    raw_prefix: String::new(),
                    flag: 1,
                    data_byte1: if s.apply_effect { 0x82 } else { 0x80 },
                    apply_ready: s.apply_effect,
                })
            })
        }
        fn set_data(&self, address: u8, value: u8) -> Result<()> {
            STATE.with(|state| {
                let mut s = state.borrow_mut();
                match address {
                    0xd1 => {
                        assert_eq!(s.persisted_phase, Some(Phase::TriggerAttempted));
                        assert_eq!(value, 0x81);
                        s.trigger_attempts += 1;
                        s.apply_effect = true;
                        if s.failure == Failure::TriggerReply {
                            bail!("call applied but reply failed");
                        }
                    }
                    0xbe => {
                        assert_eq!(s.persisted_phase, Some(Phase::AcknowledgementAttempted));
                        assert_eq!(value, 2);
                        s.ack_attempts += 1;
                        if s.failure == Failure::AckReply {
                            bail!("acknowledgement reply lost");
                        }
                    }
                    _ => panic!("uncharacterized address"),
                }
                Ok(())
            })
        }
    }
    impl Platform for Fake {
        type Acpi = FakeAcpi;
        fn is_elevated() -> Result<bool> {
            Ok(true)
        }
        fn read_msi_variable() -> Result<FirmwareVariable> {
            STATE.with(|state| {
                let mut s = state.borrow_mut();
                s.reads += 1;
                if s.failure == Failure::StageRead && s.reads == 4 {
                    bail!("stage read failed");
                }
                if s.failure == Failure::Stale && s.reads == 2 {
                    s.variable.bytes[5] &= !0x10;
                }
                if s.failure == Failure::JournalStale && s.reads == 3 {
                    s.variable.bytes[5] = 0x32;
                }
                Ok(s.variable.clone())
            })
        }
        fn write_msi_variable(variable: &FirmwareVariable) -> Result<()> {
            STATE.with(|state| {
                let mut s = state.borrow_mut();
                s.writes += 1;
                if s.writes == 1 {
                    assert_eq!(s.persisted_phase, Some(Phase::StageAttempted));
                }
                if s.failure == Failure::BeforeWrite && s.writes == 1 {
                    bail!("write rejected");
                }
                s.variable = variable.clone();
                if s.failure == Failure::AfterWrite && s.writes == 1 {
                    bail!("write applied but syscall reply failed");
                }
                Ok(())
            })
        }
        fn secure_boot_enabled() -> Result<bool> {
            Ok(false)
        }
        fn query_machine() -> Result<MachineInfo> {
            STATE.with(|state| {
                let mut s = state.borrow_mut();
                s.queries += 1;
                let mut m = machine();
                if s.failure == Failure::MachineChanged && s.queries == 2 {
                    m.bios = "unknown".into();
                }
                Ok(m)
            })
        }
        fn query_display_devices() -> Result<Vec<DisplayDevice>> {
            Ok(Vec::new())
        }
        fn ac_power_online() -> Result<bool> {
            STATE.with(|state| {
                let mut s = state.borrow_mut();
                s.power_queries += 1;
                Ok(!(s.failure == Failure::PowerLost && s.power_queries == 2))
            })
        }
        fn query_registry_state() -> BTreeMap<String, Option<u32>> {
            BTreeMap::new()
        }
        fn backup_directory() -> Result<PathBuf> {
            panic!("fake transaction must use injected journal")
        }
        fn privilege_requirement() -> &'static str {
            "root"
        }
        fn recovery_key_warning() -> &'static str {
            ""
        }
        fn shutdown_command() -> &'static str {
            ""
        }
        fn verification_command() -> &'static str {
            ""
        }
        fn wait_for_keypress() {}
        fn boot_id() -> Result<String> {
            Ok("test-boot".into())
        }
    }
    #[derive(Default)]
    struct MemoryJournal {
        record: Option<TransactionRecord>,
        saved: Vec<Phase>,
        fail_phase: Option<Phase>,
    }
    impl Journal for MemoryJournal {
        fn load(&self) -> Result<Option<TransactionRecord>> {
            Ok(self.record.clone())
        }
        fn save(&mut self, record: &TransactionRecord) -> Result<()> {
            if self.fail_phase == Some(record.phase) {
                bail!("injected durable journal failure");
            }
            self.record = Some(record.clone());
            self.saved.push(record.phase);
            STATE.with(|s| s.borrow_mut().persisted_phase = Some(record.phase));
            Ok(())
        }
    }
    fn run(failure: Failure) -> (Result<()>, MemoryJournal) {
        STATE.with(|s| *s.borrow_mut() = fresh(failure));
        let mut journal = MemoryJournal::default();
        let result = apply::<Fake, _>(Mode::Discrete, &mut journal, |_, _, _| {});
        (result, journal)
    }
    #[cfg(target_os = "linux")]
    #[test]
    fn operation_lock_rejects_competing_descriptors_and_releases_on_drop() {
        let path = std::env::temp_dir().join(format!("msi-mux-lock-test-{}", std::process::id()));
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .create_new(true)
            .open(&path)
            .unwrap();
        let guard = OperationLock::lock_file(file).unwrap();
        let second = OpenOptions::new()
            .read(true)
            .write(true)
            .open(&path)
            .unwrap();
        assert!(OperationLock::lock_file(second).is_err());
        drop(guard);
        let third = OpenOptions::new()
            .read(true)
            .write(true)
            .open(&path)
            .unwrap();
        drop(OperationLock::lock_file(third).unwrap());
        fs::remove_file(path).unwrap();
    }

    #[test]
    fn successful_apply_is_pending_and_preserves_non_target_bits() {
        let (result, journal) = run(Failure::None);
        result.unwrap();
        assert_eq!(journal.record.unwrap().phase, Phase::PendingShutdown);
        STATE.with(|s| {
            let s = s.borrow();
            assert_eq!(s.writes, 1);
            assert_eq!(s.variable.bytes[5], 0x31);
            assert_eq!(s.ack_attempts, 1);
            assert_eq!(s.variable.bytes[9], 0xa5);
        });
    }
    #[test]
    fn preflight_revalidation_rejects_stale_hardware_power_or_firmware() {
        for f in [
            Failure::Stale,
            Failure::MachineChanged,
            Failure::PowerLost,
            Failure::Busy,
        ] {
            let (result, journal) = run(f);
            assert!(result.is_err());
            assert!(journal.saved.is_empty());
            STATE.with(|s| assert_eq!(s.borrow().writes, 0));
        }
    }
    #[test]
    fn stale_target_after_journaling_never_gets_overwritten_by_restoration() {
        let (result, journal) = run(Failure::JournalStale);
        assert!(
            result
                .unwrap_err()
                .to_string()
                .contains("no firmware write")
        );
        assert_eq!(journal.record.unwrap().phase, Phase::Prepared);
        STATE.with(|s| {
            assert_eq!(s.borrow().writes, 0);
            assert_eq!(s.borrow().variable.bytes[5], 0x32);
        });
    }

    #[test]
    fn writes_that_fail_before_or_after_application_restore_target() {
        for f in [
            Failure::BeforeWrite,
            Failure::AfterWrite,
            Failure::StageRead,
        ] {
            let (result, journal) = run(f);
            assert!(result.is_err());
            let record = journal.record.unwrap();
            assert!(record.target_restoration_verified);
            assert!(!record.trigger_attempted);
            STATE.with(|s| {
                assert_eq!(s.borrow().variable.bytes[5], 0x30);
                assert_eq!(s.borrow().trigger_attempts, 0);
            });
        }
    }
    #[test]
    fn failed_trigger_and_failed_output_reads_are_ambiguous_even_when_target_restored() {
        for f in [
            Failure::TriggerReply,
            Failure::TriggerRead,
            Failure::AckReply,
        ] {
            let (result, journal) = run(f);
            assert!(
                result
                    .unwrap_err()
                    .to_string()
                    .contains("state may have changed")
            );
            let record = journal.record.unwrap();
            assert!(record.trigger_attempted);
            assert!(record.target_restoration_verified);
            assert!(journal.saved.contains(&Phase::TriggerAttempted));
            STATE.with(|s| assert!(s.borrow().apply_effect));
            let info = FirmwareInfo::decode(&fresh(Failure::None).variable).unwrap();
            assert!(pending_reason(Some(&record), &info, "test-boot").is_some());
        }
    }
    #[test]
    fn journal_failure_before_stage_prevents_any_firmware_write() {
        STATE.with(|s| *s.borrow_mut() = fresh(Failure::None));
        let mut journal = MemoryJournal {
            fail_phase: Some(Phase::StageAttempted),
            ..Default::default()
        };
        assert!(apply::<Fake, _>(Mode::Discrete, &mut journal, |_, _, _| {}).is_err());
        STATE.with(|s| assert_eq!(s.borrow().writes, 0));
    }
    #[test]
    fn journal_failure_before_trigger_prevents_acpi_trigger() {
        STATE.with(|s| *s.borrow_mut() = fresh(Failure::None));
        let mut journal = MemoryJournal {
            fail_phase: Some(Phase::TriggerAttempted),
            ..Default::default()
        };
        assert!(apply::<Fake, _>(Mode::Discrete, &mut journal, |_, _, _| {}).is_err());
        STATE.with(|s| {
            assert_eq!(s.borrow().trigger_attempts, 0);
            assert_eq!(s.borrow().variable.bytes[5], 0x30);
        });
    }
    #[test]
    fn unrelated_firmware_change_is_never_overwritten_by_restoration() {
        let (result, journal) = run(Failure::OtherByteChanged);
        assert!(result.is_err());
        assert!(!journal.record.unwrap().target_restoration_verified);
        STATE.with(|s| {
            assert_eq!(s.borrow().writes, 1);
            assert_eq!(s.borrow().variable.bytes[9], 0xa4);
        });
    }
    #[test]
    fn repeat_apply_is_blocked_and_reboot_requires_verified_reconciliation() {
        let (_, mut journal) = run(Failure::None);
        STATE.with(|s| {
            let mut s = s.borrow_mut();
            s.writes = 0;
            s.apply_effect = false;
        });
        assert!(apply::<Fake, _>(Mode::Integrated, &mut journal, |_, _, _| {}).is_err());
        STATE.with(|s| assert_eq!(s.borrow().writes, 0));
        let mut info = FirmwareInfo::decode(&fresh(Failure::None).variable).unwrap();
        let record = journal.record.as_ref().unwrap();
        assert!(pending_reason(Some(record), &info, "another-boot").is_some());
        info.current_mode = Some(Mode::Discrete);
        info.selected_target_mode = Some(Mode::Discrete);
        assert!(pending_reason(Some(record), &info, "another-boot").is_none());
        assert!(pending_reason(Some(record), &info, "test-boot").is_some());
    }
    #[test]
    fn foreign_journal_overrides_target_mismatch_with_recovery() {
        let (_, journal) = run(Failure::None);
        let mut record = journal.record.unwrap();
        let mut info = FirmwareInfo::decode(&fresh(Failure::None).variable).unwrap();
        info.selected_target_mode = Some(Mode::Discrete);
        for field in ["model", "board", "bios"] {
            let original = record.clone();
            match field {
                "model" => record.model = "foreign".into(),
                "board" => record.board = "foreign".into(),
                _ => record.bios = "foreign".into(),
            }
            let state = pending_state(Some(&record), &info, "test-boot");
            assert!(matches!(state, PendingState::RecoveryRequired(_)));
            assert!(!state.needs_shutdown());
            assert!(state.reason().unwrap().contains("different hardware"));
            record = original;
        }
        assert!(pending_state(Some(&record), &info, "test-boot").needs_shutdown());
    }

    #[test]
    fn unknown_record_or_current_boot_never_claims_routine_shutdown() {
        let (_, journal) = run(Failure::None);
        let mut record = journal.record.unwrap();
        let mut info = FirmwareInfo::decode(&fresh(Failure::None).variable).unwrap();
        info.selected_target_mode = Some(Mode::Discrete);
        for invalid in ["", "unknown-boot", " "] {
            record.boot_id = invalid.into();
            let state = pending_state(Some(&record), &info, "test-boot");
            assert!(matches!(state, PendingState::RecoveryRequired(_)));
            assert!(!state.needs_shutdown());
            record.boot_id = "test-boot".into();
            let state = pending_state(Some(&record), &info, invalid);
            assert!(matches!(state, PendingState::RecoveryRequired(_)));
            assert!(!state.needs_shutdown());
            assert!(matches!(
                pending_state(None, &info, invalid),
                PendingState::RecoveryRequired(_)
            ));
        }
    }

    #[test]
    fn malformed_layout_or_unknown_modes_never_write() {
        for index in 0..3 {
            STATE.with(|s| {
                let mut s = s.borrow_mut();
                *s = fresh(Failure::None);
                match index {
                    0 => s.variable.bytes.push(0),
                    1 => s.variable.attributes = 6,
                    _ => s.variable.bytes[5] = 0x3f,
                }
            });
            let mut journal = MemoryJournal::default();
            assert!(apply::<Fake, _>(Mode::Discrete, &mut journal, |_, _, _| {}).is_err());
            STATE.with(|s| assert_eq!(s.borrow().writes, 0));
        }
    }
}
