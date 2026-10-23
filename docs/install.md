# Installation

[Italiano](it/install.md) | [Back to the README](../README.md)

On this page: [what you need](#what-you-need), [installing the requirements](#installing-the-requirements),
[getting the NVIDIA files](#getting-the-nvidia-files), [installing](#installing), [starting it](#starting-it).

## What you need

- An NVIDIA RTX GPU with the proprietary driver. The CUDA and NGX libraries must be present on the host.
- `umu-launcher` (`umu-run`), PipeWire with `pw-loopback`, `python3` with `venv`, and `p7zip` to unpack the Windows driver.
- GE-Proton11-11 (x86_64 build) unpacked as `~/.local/share/Steam/compatibilitytools.d/GE-Proton11-11-x86_64`.
  UMU-Proton does not work because its `nvcuda.dll` is empty. Other GE builds need the byte patches in
  `scripts/patch-qcap.py` to be derived again, and the script refuses to apply them blindly.
- `v4l2loopback` for the webcam. On Fedora: `sudo dnf install akmod-v4l2loopback v4l2loopback` (RPM Fusion). With
  Secure Boot, the akmod module must be signed with an enrolled MOK key.
- For the Files tab with video: `gcc`, `make` and `curl`.
- The NVIDIA Broadcast Windows installer, extracted to a folder that contains `setup.exe` and `setup.cfg`, and
  `nvngx_update.exe` from a Windows NVIDIA driver package (`Display.Driver/` inside the extracted driver; version 616.64
  was used).

## Installing the requirements

Only the Fedora commands were tested. The Debian, Ubuntu and Arch package names follow each distribution's usual
naming and may differ on your release. If one is not found, search for it with your package manager.

### Fedora

Enable [RPM Fusion](https://rpmfusion.org/Configuration) first, for the NVIDIA driver and v4l2loopback.

```
sudo dnf install python3 pipewire-utils 7zip umu-launcher
sudo dnf install akmod-nvidia xorg-x11-drv-nvidia-cuda akmod-v4l2loopback v4l2loopback
```

### Debian and Ubuntu

```
sudo apt install python3 python3-venv pipewire-bin 7zip dkms linux-headers-$(uname -r) v4l2loopback-dkms
# NVIDIA driver with CUDA: Ubuntu: sudo ubuntu-drivers install   Debian: sudo apt install nvidia-driver libcuda1
```

`7zip` is called `p7zip-full` on older releases. `umu-launcher` has no apt package, so install the `.deb` files from the
[umu-launcher releases](https://github.com/Open-Wine-Components/umu-launcher/releases). With Secure Boot, DKMS asks you
to enrol a MOK key the first time (`mokutil`). Reboot and confirm it.

### Arch

```
sudo pacman -S python pipewire 7zip dkms linux-headers nvidia-utils v4l2loopback-dkms
yay -S umu-launcher            # AUR, or pacman -S umu-launcher if your repositories have it
```

For the driver itself use `nvidia-open-dkms` or `nvidia-dkms` (or `nvidia` / `nvidia-open` for the stock kernel). With
Secure Boot you have to sign the DKMS modules yourself.

### GE-Proton11-11 (all distributions)

Download the x86_64 tarball from the [GE-Proton releases](https://github.com/GloriousEggroll/proton-ge-custom/releases)
and unpack it:

```
mkdir -p ~/.local/share/Steam/compatibilitytools.d
tar -xf GE-Proton11-11*.tar.* -C ~/.local/share/Steam/compatibilitytools.d
ls ~/.local/share/Steam/compatibilitytools.d     # if the folder name differs from GE-Proton11-11-x86_64, set NVB_RUNNER
```

### Checking the NVIDIA side

Proton needs the Windows-side libraries of the driver, which the proprietary driver packages install. This command must
list `nvngx.dll`:

```
ls /usr/lib*/nvidia/wine/nvngx.dll
```

## Getting the NVIDIA files

Two files from NVIDIA are needed. They are not in this repository, and you download them yourself under NVIDIA's terms.
Keep them outside the repository, for example in `~/nvidia-files`:

```
mkdir -p ~/nvidia-files
```

### The NVIDIA Broadcast installer

The file is `NVIDIA_Broadcast_v2.3.0.<build>.exe`, about 190 MB. Download it from the
[NVIDIA Broadcast page](https://www.nvidia.com/en-us/geforce/broadcasting/broadcast-app/) into `~/nvidia-files`. It is a
self-extracting archive that unpacks to about 730 MB. Extract it with 7-Zip instead of running it:

```
7z x ~/nvidia-files/NVIDIA_Broadcast_v2.3.0.*.exe -o$HOME/nvidia-files/broadcast-installer
ls ~/nvidia-files/broadcast-installer        # must contain setup.exe, setup.cfg, NvMaxineClient, NvMaxineModels ...
```

`broadcast-installer` is the folder you pass as `--installer`. The version tested is 2.3.0.

### nvngx_update.exe

This is the model updater, about 1 MB, and it comes from the Windows NVIDIA driver package. On the
[NVIDIA driver download page](https://www.nvidia.com/Download/index.aspx) choose Windows 10/11 64-bit and your GPU, and
download the `.exe` into `~/nvidia-files` (the version tested is 616.64). Extract only that file:

```
7z x ~/nvidia-files/616.64-*.exe -o$HOME/nvidia-files/driver Display.Driver/nvngx_update.exe
ls ~/nvidia-files/driver/Display.Driver/nvngx_update.exe
```

The driver installer is also a 7-Zip archive, so `7z` opens it without Windows. If the file is not at that path in your
driver version, search the listing with `7z l driver.exe | grep -i ngx`. The path of the extracted file is what you pass
as `--nvngx-update`. The Linux `nvidia-ngx-updater` cannot replace it, because NVIDIA's model server rejects its
platform.

When the installation is finished you can delete the 730 MB folder and the downloaded `.exe` files. Only
`setup-prefix.sh` reads them.

## Installing

```
git clone <this repo> && cd broadcast-linux

scripts/build.sh                      # builds everything into build/ (installs Zig in ~/.cache/nvb-build, no root)
scripts/build-ffmpeg.sh               # only for the Files tab with video: downloads FFmpeg 8.1 and builds two parts
scripts/setup-prefix.sh --installer ~/nvidia-files/broadcast-installer \
                        --nvngx-update ~/nvidia-files/driver/Display.Driver/nvngx_update.exe
scripts/deploy.sh                     # installs the shims and patches into the runner and the prefix
sudo scripts/setup-vcam.sh            # webcam only; add --obs if you use OBS Studio (see obs.md)
```

`setup-prefix.sh` was written from the manual steps that worked and has not been run again on a fresh prefix. If a step
fails, the comments in the script say what each step is for.

The environment variables `NVB_RUNNER` and `NVB_PREFIX` override the runner and prefix paths in all scripts.

## Starting it

Run `nvidia-broadcast`. `deploy.sh` links the launcher into `~/.local/bin`. The launcher creates the PipeWire devices,
starts the camera feeder if a v4l2loopback device labelled "NVIDIA Broadcast" exists, and starts the app.

Start Broadcast before the applications that use its devices. To stop it, use Exit from the tray icon (right click). If
a session gets stuck, kill it with:

```
WINEPREFIX=~/Games/umu/nvbroadcast/pfx ~/.local/share/Steam/compatibilitytools.d/GE-Proton11-11-x86_64/files/bin/wineserver -k
```

Inside Broadcast, select your real devices as microphone, speaker and camera. In Discord, OBS, a browser and so on, select
"NVIDIA Broadcast" as microphone, speaker or camera. Do not select "NVIDIA Broadcast" as the camera inside Broadcast
itself, because that would feed it its own output.

### Application menu entry

`deploy.sh` also installs `~/.local/share/applications/nvidia-broadcast.desktop` from the template
`nvidia-broadcast.desktop`, so "NVIDIA Broadcast" shows up in the application menu and can be pinned. The entry starts
the launcher. It does not start the Wine shortcut that Proton creates in the prefix, which would skip the PipeWire
devices and the camera feeder.

The icon is the one Proton extracted from the app into `<prefix>/pfx/drive_c/proton_shortcuts/icons/`. It is
referenced from there and not copied into this repository. If it is not found, the generic `camera-video` icon is used.
`StartupWMClass` lets the desktop group the window with the menu entry.
