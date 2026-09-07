# Recovery and first hardware use

Version `0.5.0` offers local drafts for Hybrid, Discrete and Integrated on the exact configuration in [the validation record](VALIDATION.md). Apply saves a draft; the desktop application changes firmware only after an explicit Restart/Power Off choice inside MSI MUX. Three captured Hybrid/Discrete tests used full shutdown/power-on cycles; the device owner separately reported Integrated hardware success without a recorded transition direction or boot method. These successful normal transitions do not establish recovery from a firmware failure.

Before a first transition, retain the working BIOS version, use the original AC adapter, save work, and keep access to another device for recovery instructions. Have a bootable Linux environment and any disk-encryption recovery material available. USB recovery requires that the laptop can still boot with a usable display or another already-configured access path.

On the characterized laptop, HDMI is wired to NVIDIA and Thunderbolt display paths to Intel. External display is a possible fallback, not a guarantee. [MSI model table](https://storage-asset.msi.com/global/picture/faq/10017910%402025-0717-0218-131248%40kb_10880_en.pdf#page=5)

## Local drafts and committed targets

Apply only saves or changes your local draft. Later sends no firmware or power request. You can cancel an uncommitted draft or select the current mode to clear it. A valid draft returns if you reopen MSI MUX during the same boot; it is invalidated after a new boot without assuming that the selected mode was applied. Restarting or shutting down from the desktop's own menu does not apply a draft.

Save your work before selecting Restart or Power Off inside MSI MUX. The application verifies fresh status and the latest draft, requests helper authorization, and applies firmware. Only verified success permits the chosen normal desktop power request. If firmware application fails or its result is uncertain, no power request follows.

The desktop may show confirmation, report unsaved-work inhibitors or allow cancellation after firmware success. MSI MUX does not force the action or try another transport if the desktop declines or its reply is uncertain. **Canceling a desktop power request does not undo a firmware transaction that already succeeded.** The target can remain pending until a suitable boot.

An existing firmware-pending transaction, including one made by an older release or the CLI, is not an editable draft. Canceling a local choice, clearing preferences or reopening the application does not roll it back. The application preserves pending-transaction and apply-ready protections. See the [retargeting evidence limit](VALIDATION.md#retargeting-an-acknowledged-transaction).

Full shutdown followed by manual power-on is the method established by the three captured hardware tests. The effectiveness of a warm restart for the MUX change has not been established. A saved draft, firmware-pending target, animation or accepted desktop power request is not proof of the new physical mode. Check current/target agreement and the internal display's GPU after the next boot.

The CLI retains immediate firmware application after its normal confirmation; it does not save a desktop draft or automatically perform a power action.

## Failed operations

After a failed firmware operation, do not repeatedly attempt mode commits or delete transaction metadata to enable a button. This restriction concerns firmware transactions, not editing an uncommitted local draft. Save the error and inspect the recorded transaction state. Restoring target bits does not prove that the EC did not act. If the application reports that manual recovery is required, resolve that condition before retrying; a foreign, malformed, or unreconciled transaction is not a routine pending shutdown.

If a trigger may have been sent, follow the explicit application instructions and shut down fully only after saving work. On the next boot, compare requested/current firmware modes and actual internal display routing. A mismatch requires investigation.

If Windows can still be started, MSI Center may provide a route back to MSHybrid. This depends on booting and accessing that tool; it is not a guaranteed fallback.

## BIOS defaults, EC reset, reflashing

- **Load Optimized Defaults** requires BIOS access. MSI does not document that it resets this tool's specific OEM variable.
- **EC/battery reset** can resolve power and initialization state. It is not a full BIOS image restore.
- **BIOS reflashing** is a separate operation. This project has not verified a display-independent user recovery procedure for this model.

The [MS-15M3/MS-15M4 manual, page 2-4](https://download.msi.com/archive/mnu_exe/nb/MS-15M3_MS-15M4_v1.0_English.pdf#page=18) describes battery reset: disconnect AC, hold power for 20 seconds until the LED blinks, continue five seconds until it turns off, reconnect AC, and power on. Do not insert objects into an unidentified case hole.

MSI's [black-screen guide](https://www.msi.com/faq/notebook-9990) directs users to service if the logo does not appear after EC reset. Neither EC reset nor BIOS defaults promises recovery from every firmware failure.

## Records

Transaction metadata records modes, stage, boot identity, timing, and verification information. It is not a raw firmware dump or a full backup. Hybrid/Discrete switching has captured live evidence in the validation record; Integrated has the owner's separate success report. Real failure recovery and warm-restart effectiveness remain unverified. Synthetic fault-injection tests cover software failure handling; do not intentionally induce a firmware failure to test recovery.
