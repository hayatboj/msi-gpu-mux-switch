# MSI MUX for Linux desktops

[Türkçe](README.tr.md) · [Releases](https://github.com/hayatboj/msi-gpu-mux-switch/releases) · [GNOME setup](https://github.com/hayatboj/msi-gpu-mux-switch/blob/linux-kde/docs/GNOME.md) · [Recovery](docs/RECOVERY.md) · [Security](SECURITY.md)

A native Qt 6 application for controlling the GPU MUX on a supported MSI laptop. See the current graphics mode, choose a mode from the desktop tray, and switch between English and Turkish without restarting the application. KDE uses its system tray; GNOME can show the same indicator and menu through AppIndicator support.

**Version 0.4.0 offers Hybrid, Discrete and Integrated as ordinary mode choices on the exact configuration below.** Three captured Linux transitions—**MSHybrid → Discrete → MSHybrid → Discrete**—succeeded on 2026-09-07 after manual full shutdowns and power-ons, retaining **2560×1600 at 240 Hz**. The device owner subsequently reported a successful Integrated hardware test. That report is separate from the three recorded post-boot observations: its transition direction and boot method were not recorded. See the [evidence and its limits](docs/VALIDATION.md).

| Supported configuration | Value |
|---|---|
| Model | MSI Vector 16 HX AI A2XWIG |
| Motherboard | MS-15M3 |
| BIOS | E15M3IMS.116 |
| Platform | Linux x86-64, UEFI boot |
| Desktop | KDE Plasma; GNOME with an AppIndicator/StatusNotifierItem adapter |

Other MSI laptops and BIOS versions are not enabled by the desktop application. Having an NVIDIA GPU, or a similarly named MSI model, is not sufficient.

![English KDE interface, using synthetic demo data](docs/images/kde-en.png)

## Features

- Current firmware mode and requested next mode shown separately.
- Hybrid, Discrete and Integrated choices when supported by the firmware and safety checks.
- Red Discrete, amber Hybrid and teal Integrated visuals, with animated pending feedback and an optional reduced-motion preference.
- About, What's new and System information, including the actual GPU driving the internal panel.
- KDE and GNOME tray menus, compact status window, English/Turkish UI, optional login startup.
- Polkit authorization through a narrowly scoped helper; the GUI never runs as root.
- Read-only polling without MSI ACPI calls or `nvidia-smi` GPU wakeups.
- Serialized firmware transactions and durable transaction metadata.
- Explicit mode confirmation, then Restart, Power Off or Later choices after a successful request; no automatic power action.

This changes the hardware graphics mode. PRIME render offload selects the GPU used by an application and is a different operation.

## Using the tray

Open **MSI MUX** from the application launcher. Open the tray icon's menu to see the current mode and choose an available mode. On KDE, right-click opens the menu and left-click opens the window. The language menu switches between **English** and **Türkçe**. Startup at login and reduced motion are optional. **About** contains release notes and system details, keeping the main window focused on mode selection.

All three modes use the same confirmation and hardware checks. The indicator distinguishes the current mode from the requested target; a pending animation does not mean the physical display connection has already changed.

Switching requires AC power, matching hardware/BIOS, readable firmware state, and no unresolved previous transaction. After a successful request, choose **Restart**, **Power Off**, or **Later**. A power action is sent only after your explicit choice, through the active desktop's normal session interface. The desktop may ask for confirmation, report an inhibitor, or allow cancellation; MSI MUX does not force the action or bypass that handling.

Save your work before choosing a power action. **A full shutdown and power-on is the path demonstrated by the three captured tests; warm-restart effectiveness has not been established.** After starting again, verify both the reported current mode and the internal display's GPU. On this laptop, Discrete mode disables display output through the Intel-wired Thunderbolt ports; HDMI is wired to NVIDIA.

On GNOME, enable the maintained AppIndicator adapter to place the existing icon and menu in the standard top bar; other panel extensions may change placement. The application offers setup guidance when GNOME has no tray host, and keeps its normal window available. Launcher actions for all three modes also open the application's confirmation flow. See [GNOME setup and compatibility](https://github.com/hayatboj/msi-gpu-mux-switch/blob/linux-kde/docs/GNOME.md). A native GNOME session has not been tested on the development host.

## Install

Use the Linux archive and `SHA256SUMS` from [this fork's releases](https://github.com/hayatboj/msi-gpu-mux-switch/releases). Check the release's hardware validation notes. Extract the archive, then run from the extracted directory:

```sh
python3 install.py --check
sudo python3 install.py
```

Remove the archive installation with its `uninstall.py`. Do not install both the archive bundle and Arch package at the same time. Installation does not enable login startup automatically.

The archive targets Linux x86-64 and requires Qt 6 and Polkit. Distribution-native building is preferred when the release binary's runtime requirements do not match your distribution.

## Build

Development dependencies: Rust 1.88+, CMake, a C++20 compiler, Python 3, and Qt 6 Core, Gui, Widgets, Network, DBus, Svg, and Test. On Arch/CachyOS these are provided by `base-devel`, `cmake`, `ninja`, `rust`, `python`, `qt6-base`, and `qt6-svg`; runtime authorization uses `polkit` and a desktop authentication agent.

```sh
./scripts/build-linux.sh
./scripts/package-linux.sh 0.4.0
```

Arch packaging is in [`packaging/`](packaging/). The running kernel needs the MSI WMI platform driver for detailed diagnostics and mode changes.

| Installed path | Purpose |
|---|---|
| `/usr/bin/msi-mux-tray` | Unprivileged desktop application |
| `/usr/bin/msi-mux-switch` | Rust backend |
| `/usr/lib/msi-mux/msi-mux-helper` | Restricted privileged helper |
| `/usr/share/polkit-1/actions/org.hayatboj.msimux.policy` | Authentication policy |
| `/var/lib/msi-mux/` | Root-controlled transaction metadata |

The helper accepts only supported mode names and diagnostics. It does not accept a program path, shell command, arbitrary firmware value, or hardware override from the GUI.

## Read-only diagnostics

Routine status does not invoke ACPI:

```sh
msi-mux-switch --status --json
```

Detailed diagnostics additionally query MSI WMI when permission is available:

```sh
msi-mux-switch --debug --json
```

A successful diagnostic is not proof that physical switching or recovery has succeeded.

Human-readable CLI switching requires the mode-specific typed confirmation for all three modes. `--json` is intended for scripts and skips that prompt only; it retains all backend safety checks and provides no hardware or BIOS override. Desktop `--request-mode` actions request the normal GUI confirmation; they do not use the scripted switching path.

## Development

Read [CONTRIBUTING.md](CONTRIBUTING.md), [AGENTS.md](AGENTS.md), and the [validation record](docs/VALIDATION.md). CI tests synthetic firmware failures, GUI parsing and gating, localization, helper restrictions, and installation boundaries. It also builds the preserved Windows CLI.

Release bundles are built only from `v*` tag pushes or manual workflow dispatch. Publication requires a matching version tag and successful Linux and Windows validation/build jobs.

Never run a real firmware write or desktop power action as a unit test, installation check, screenshot step, or CI action. Hardware validation is separate: record the transition and boot method, then verify the resulting firmware mode and physical display route.

## Limits

The tool writes an OEM UEFI variable and invokes MSI ACPI methods. Failure after a trigger can leave the hardware outcome uncertain. Returning previous target bits does not reverse every hardware action. BIOS defaults or an EC reset are not guaranteed recovery. The captured Hybrid/Discrete tests and owner-reported Integrated success concern the listed configuration; they do not establish a universal recovery procedure or prove warm-restart behavior. Synthetic tests cover failure handling without deliberately disrupting real firmware. Read [the recovery notes](docs/RECOVERY.md) before first use.

## Upstream and license

Forked from [steelbrain/msi-gpu-mux-switch](https://github.com/steelbrain/msi-gpu-mux-switch). Original copyright notices and Windows protocol work are retained. This fork adds the Linux desktop application, KDE/GNOME integration, packaging, and transaction hardening. [MIT license](LICENSE). Independent project; not affiliated with MSI or NVIDIA.
