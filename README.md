# NVIDIA Broadcast on Linux (Wine / GE-Proton)


[Versione italiana](README.it.md)

Runs the real Windows **NVIDIA Broadcast 2.3.0** on Linux, with the genuine Maxine models, and exposes its output to
Linux applications as a microphone, a speaker and a webcam:

| Feature | Status |
|---|---|
| Microphone effects (noise removal, room echo) | works, appears as PipeWire source "NVIDIA Broadcast" |
| Speaker effects (noise removal on incoming audio) | works, appears as PipeWire sink "NVIDIA Broadcast" |
| Camera effects (background blur / replacement, eye contact, auto frame) | works, appears as webcam "NVIDIA Broadcast" (v4l2loopback) |
| Processing audio/video files | not tested |

This repository contains **only my own sources and scripts**. It contains no NVIDIA binaries, installers, models or
drivers, and it is not affiliated with, endorsed by or supported by NVIDIA. You need your own copy of the NVIDIA
Broadcast installer and of a Windows NVIDIA driver package, under NVIDIA's terms. The project is meant for personal
use; it modifies a Wine runner and a Wine prefix on your machine and replaces some of NVIDIA's user-mode DLLs inside the
prefix with compatible stand-ins (see [How it works](#how-it-works)).

It was developed and tested on one machine only: Fedora 44 (kernel 7.2), KDE Wayland, PipeWire, RTX 4060 Laptop GPU
with the proprietary driver 615.78.08, Secure Boot on. Expect to adapt things on other setups (see
[Fragile points](#fragile-points)).

## Requirements

- An NVIDIA RTX GPU with the proprietary driver (CUDA and NGX libraries must be present on the host).
- `umu-launcher` (`umu-run`), PipeWire with `pw-loopback`, `python3` with `venv`, `p7zip` (to unpack the Windows driver).
- **GE-Proton11-11** (x86_64 build) unpacked as `~/.local/share/Steam/compatibilitytools.d/GE-Proton11-11-x86_64`.
  UMU-Proton does not work: its `nvcuda.dll` is empty. Other GE builds need the byte patches in
  `scripts/patch-qcap.py` to be re-derived, which the script refuses to do blindly.
- `v4l2loopback` for the webcam. On Fedora: `sudo dnf install akmod-v4l2loopback v4l2loopback` (RPM Fusion). With
  Secure Boot the akmod module must be signed with an enrolled MOK key.
- The NVIDIA Broadcast Windows installer (extracted, a folder containing `setup.exe` and `setup.cfg`), and
  `nvngx_update.exe` from a Windows NVIDIA driver package (`Display.Driver/` inside the extracted driver; version 616.64
  was used).

## Installing the requirements

Only the Fedora column was tested. Package names for Debian/Ubuntu and Arch come from the distributions' usual naming
and may differ on your release: check with your package manager if one is not found.

**Fedora** (with [RPM Fusion](https://rpmfusion.org/Configuration) enabled for the NVIDIA driver and v4l2loopback):

```
sudo dnf install python3 pipewire-utils 7zip umu-launcher
sudo dnf install akmod-nvidia xorg-x11-drv-nvidia-cuda akmod-v4l2loopback v4l2loopback
```

**Debian / Ubuntu**:

```
sudo apt install python3 python3-venv pipewire-bin 7zip dkms linux-headers-$(uname -r) v4l2loopback-dkms
# NVIDIA driver with CUDA: Ubuntu: sudo ubuntu-drivers install   Debian: sudo apt install nvidia-driver libcuda1
```

`7zip` is called `p7zip-full` on older releases. `umu-launcher` has no apt package: install the `.deb` files from the
[umu-launcher releases](https://github.com/Open-Wine-Components/umu-launcher/releases). With Secure Boot, DKMS asks you
to enrol a MOK key the first time (`mokutil`), then reboot and confirm it.

**Arch**:

```
sudo pacman -S python pipewire 7zip dkms linux-headers nvidia-utils v4l2loopback-dkms
yay -S umu-launcher            # AUR, or pacman -S umu-launcher if your repositories have it
```

Use `nvidia-open-dkms` or `nvidia-dkms` (or `nvidia`/`nvidia-open` for the stock kernel) for the driver itself. With
Secure Boot you have to sign the DKMS modules yourself.

**GE-Proton11-11** (all distributions): download the x86_64 tarball from the
[GE-Proton releases](https://github.com/GloriousEggroll/proton-ge-custom/releases) and unpack it:

```
mkdir -p ~/.local/share/Steam/compatibilitytools.d
tar -xf GE-Proton11-11*.tar.* -C ~/.local/share/Steam/compatibilitytools.d
ls ~/.local/share/Steam/compatibilitytools.d     # if the folder name differs from GE-Proton11-11-x86_64, set NVB_RUNNER
```

**Check the NVIDIA side**: Proton needs the driver's Windows-side libraries, which the proprietary driver packages
install. This must list `nvngx.dll`:

```
ls /usr/lib*/nvidia/wine/nvngx.dll
```

## Gathering files

Two files from NVIDIA are needed. They are not in this repository and you download them yourself, under NVIDIA's terms.
Keep them outside the repository, for example in `~/nvidia-files`:

```
mkdir -p ~/nvidia-files
```

**1. The NVIDIA Broadcast installer** (`NVIDIA_Broadcast_v2.3.0.<build>.exe`, about 190 MB). Download it from the
[NVIDIA Broadcast page](https://www.nvidia.com/en-us/geforce/broadcasting/broadcast-app/) and put it in
`~/nvidia-files`. It is a self-extracting archive that unpacks to about 730 MB; extract it with 7-Zip instead of
running it:

```
7z x ~/nvidia-files/NVIDIA_Broadcast_v2.3.0.*.exe -o$HOME/nvidia-files/broadcast-installer
ls ~/nvidia-files/broadcast-installer        # must contain setup.exe, setup.cfg, NvMaxineClient, NvMaxineModels ...
```

`broadcast-installer` is the folder you pass as `--installer`. The version tested is 2.3.0.

**2. `nvngx_update.exe`** (model updater, about 1 MB) from the **Windows** NVIDIA driver package. On the
[NVIDIA driver download page](https://www.nvidia.com/Download/index.aspx) choose Windows 10/11 64-bit and your GPU,
download the `.exe` into `~/nvidia-files` (the version tested is 616.64) and extract only that file:

```
7z x ~/nvidia-files/616.64-*.exe -o$HOME/nvidia-files/driver Display.Driver/nvngx_update.exe
ls ~/nvidia-files/driver/Display.Driver/nvngx_update.exe
```

The installer is a 7-Zip archive too, so `7z` can open it without Windows. If the file is not at that path in your
driver version, search the listing: `7z l driver.exe | grep -i ngx`. The path of the extracted file is what you pass as
`--nvngx-update`. The Linux `nvidia-ngx-updater` cannot replace it: NVIDIA's model server rejects its platform.

Once the installation is finished you can delete the 730 MB folder and the downloaded `.exe` files; they are only read
by `setup-prefix.sh`.

## Install

```
git clone <this repo> && cd broadcast-linux

scripts/build.sh                      # builds everything into build/ (installs Zig in ~/.cache/nvb-build, no root)
scripts/build-ffmpeg.sh               # optional, only for the Files tab with video: downloads FFmpeg 8.1 and builds two parts
scripts/setup-prefix.sh --installer ~/nvidia-files/broadcast-installer \
                        --nvngx-update ~/nvidia-files/driver/Display.Driver/nvngx_update.exe
scripts/deploy.sh                     # installs the shims and patches into the runner and the prefix
sudo scripts/setup-vcam.sh            # webcam only; add --obs if you use OBS Studio (see below)
```

`setup-prefix.sh` was written from the manual steps that worked and has not been re-run on a fresh prefix. If a step
fails, the comments in the script say what each step is for.

Start it with `nvidia-broadcast` (`deploy.sh` links the launcher into `~/.local/bin`). The launcher creates the
PipeWire devices, starts the camera feeder if a v4l2loopback device labelled "NVIDIA Broadcast" exists, and starts the
app. **Start Broadcast before the applications that use its devices.** Stop it by closing the window; to kill a stuck
session use `WINEPREFIX=~/Games/umu/nvbroadcast/pfx ~/.local/share/Steam/compatibilitytools.d/GE-Proton11-11-x86_64/files/bin/wineserver -k`.

In Broadcast select as microphone/speaker/camera your real devices. In Discord, OBS, a browser, and so on, select
"NVIDIA Broadcast" as microphone, speaker or camera. Do not select "NVIDIA Broadcast" as the camera inside Broadcast
itself, that would feed it its own output.

Environment variables: `NVB_RUNNER` and `NVB_PREFIX` override the runner and prefix paths in all scripts.

### Application menu entry

`deploy.sh` also installs `~/.local/share/applications/nvidia-broadcast.desktop` from the template
`nvidia-broadcast.desktop`, so "NVIDIA Broadcast" shows up in the application menu and can be pinned. It starts the
launcher (not the Wine shortcut that Proton creates in the prefix, which would skip the PipeWire devices and the
camera feeder). The icon is the one Proton extracted from the app into
`<prefix>/pfx/drive_c/proton_shortcuts/icons/`; it is referenced from there and not copied into this repository. If it
is not found, the generic `camera-video` icon is used. `StartupWMClass` lets the desktop group the window with the
menu entry.

## OBS Studio coexistence

OBS's Virtual Camera also uses v4l2loopback and takes the first free loopback device. If it found ours it would write
into it. `setup-vcam.sh --obs` therefore creates two devices with **one** module option line in
`/etc/modprobe.d/99-nvidia-broadcast-vcam.conf`:

```
options v4l2loopback devices=2 video_nr=9,10 card_label="OBS Virtual Camera,NVIDIA Broadcast" exclusive_caps=1,1
```

`/dev/video9` is OBS's and has the lower number, `/dev/video10` is ours. The launcher finds ours by its label. Notes:

- Everything must be on one line: the options of several `options v4l2loopback` lines are not merged, the last value of
  a parameter wins (Fedora ships such a line for OBS in `/usr/lib/modprobe.d/98-v4l2loopback.conf`, ours is named `99-*`
  so it is read last).
- A list of labels is quoted as a whole (`"A,B"`). Quoting each name leaves stray quote characters in the names.
- OBS was not installed on the test machine; the two-device layout and the non-interference of a reader on `video9`
  were checked with a small V4L2 reader (`src/tools/v4lgrab.c`), not with OBS itself.

## How it works

The Windows app is not usable as is. Each item below was a separate failure that had to be understood and fixed.

**Installer.** `setup.exe` needs `eula.txt` next to `setup.cfg`, the registry key
`HKLM\Software\Microsoft\Windows\CurrentVersion\SharedDLLs` and the directory `C:\ProgramData\NVIDIA\NGX\models`.

**CUDA.** GE-Proton with `PROTON_NVIDIA_LIBS=1` provides `nvcuda.dll` as a wrapper over the host libcuda. Three things
are missing, so `src/nvcuda-shim` replaces the wrapper (the original stays as `nvcuda_real.dll`):
- `cuGetProcAddress` only matched exact "introduced in" versions; the shim returns the best version not above the one
  requested, as the real driver does.
- The core compares the GPU name reported by CUDA ("... 4060 Laptop GPU") with the one from DXVK-NVAPI ("... 4060M") and
  rejects a mismatch; the shim reports the NVAPI name.
- Video effects use an undocumented CUDA export table (UUID `d2688bf2-8371-e24f-9469-8177c8ccec27`, one function) that
  the Wine wrapper lacks. The shim calls the native function in the host's `libcuda.so.1` through a Windows-to-SysV
  calling convention thunk. The function sits at a fixed **offset inside libcuda of one driver version**
  (`C4G_OFFSET` in `shim.c`), and its address is found through `/proc/<pid>/maps` of another process that has libcuda
  loaded, and verified by comparing its first 11 bytes.

**NVAPI.** `src/nvapi-shim` adds `NvAPI_GPU_GetSerialNumber` (id `0x14B83A5F`) to DXVK-NVAPI (original kept as
`nvapi64_real.dll`).

**AI models.** The Linux `nvidia-ngx-updater` gets HTTP errors from NVIDIA's server; the Windows `nvngx_update.exe`
works under Wine with `PROTON_ENABLE_NGX_UPDATER=1`.

**Audio.** The Windows virtual audio driver (`nvrtxvad.sys`) cannot exist under Wine. Its user-mode library
`nvrtxaudcap64v.dll` is replaced by `src/vad-replacement`, which implements the small function table the core calls
(open session, check default endpoint, start/stop, push a 10 ms 48 kHz stereo S16 frame) and writes the frames to a
Wine render endpoint. The launcher makes PipeWire loopbacks so that this endpoint becomes a Linux microphone:
`NvB Mic Input` (sink the app writes into) to `NVIDIA Broadcast` (source). For the speaker, the sink
`NVIDIA Broadcast` is loopback-captured by the core, processed, and played on your real output.

**Webcam input.** Wine's `qcap` (the DirectShow V4L2 capture filter) has two gaps that the core does not tolerate.
`scripts/patch-qcap.py` patches the GE runner:
- `AvgTimePerFrame` is never set, and the core derives the frame rate from it. Patch in `qcap.so`.
- Frames carry no time stamp, and the core drops all frames whose `IMediaSample::GetTime` fails. Patch in `qcap.dll`
  sets a time stamp relative to the first frame (absolute times make the renderer block).

The patches use code caves in zero padding and check the original bytes first, so they only apply to the exact build
they were written for. The core itself (`NvBroadcastCore.dll`) is never modified: it verifies its own signature.

**Webcam output.** The core publishes processed frames to the DirectShow filter `NvVirtualCameraFilter_x64.dll`
through shared memory.
1. `src/vcam-bridge/bridge.c` (a Windows program started inside the prefix by the nvcuda shim when
   `NVB_VCAM_BRIDGE=1`) reads from that filter and writes BGRA frames to `/dev/shm/nvb-vcam` (layout in
   `vcam_shm.h`). It connects camera, sample grabber and null renderer directly; `RenderStream` would insert a Smart Tee
   that deadlocks.
2. `src/vcam-bridge/feeder.c` (Linux) converts to 1280x720 YUYV and writes at 30 fps to the v4l2loopback device. It
   also looks, once per second, whether any other process has the loopback device open and tells the bridge through the
   shared header. The bridge only runs its graph, and thus only keeps the real webcam on, while somebody watches the
   virtual one. Without a fresh frame for 1.5 s it sends black.

## File processing (Files tab)

Wine lacks three things the core needs to process files, all supplied without touching `NvBroadcastCore.dll` (it is
signature-checked; the shim redirects the core's imports from `mfreadwrite.dll`/`mfplat.dll` at runtime, `src/nvcuda-shim/mfhooks.c`):

- **WAV output**: Wine's sink writer cannot create a `.wav` (`MFCreateSinkWriterFromURL` fails with `0x80004003`, job error
  -9111). The shim provides a small WAV `IMFSinkWriter`.
- **Video input**: Wine lists the decoded format (NV12) before the compressed one (H264) as the native type of a video
  stream, and the core reads type 0 as the codec ("Source video codec is not supported"). The shim reorders them.
- **MP4 output**: GE-Proton's bundled `libavformat` has no muxers, Wine has no AAC encoder (`No AAC encoder MFT found`)
  and its MP4 media sink is a stub (`BeginWriting` fails with `E_NOTIMPL`). `scripts/build-ffmpeg.sh` builds a
  `libavformat` with muxers and `nvb_media.dll` (`src/media`): an AAC encoder transform and an mp4 sink writer, both on
  FFmpeg. The video itself is encoded by the core with NVENC. Needs `gcc`, `make`, `curl`.

## Fragile points

- **Driver updates**: after updating the NVIDIA driver the C4G offset in `src/nvcuda-shim/shim.c` can change. Symptom: no
  video effects, `C4G: no verified libcuda base` in the shim log (`/tmp/nvcuda_shim.log`). Find the new offset of the function whose first bytes are `55 31 c0 b9 1f 00 00 00 48 89 e5` in
  `libcuda.so.1` and rebuild.
- **Runner updates**: the qcap patches are specific to GE-Proton11-11. A runner update replaces the patched files too;
  rerun `scripts/deploy.sh` (it refuses unknown builds) and the shims are redeployed.
- **NVIDIA Broadcast updates**: the replacements of the audio library and the signature check on the core may break.
- **Runner updates and video export**: `deploy.sh` replaces GE's `libavformat.so.62.12.100` with the one built by
  `build-ffmpeg.sh`. A runner with a different FFmpeg version needs the matching FFmpeg source (the script checks).
- **GPU memory**: the app and the effects need several hundred MiB of VRAM; with other GPU-heavy programs open an
  `NV_ERR_NO_MEMORY` was seen in the kernel log once.
- Log noise that is harmless: `[CRMAIN] Received empty frame from local processor with status: 5 | Dropping frame`.

## Repository layout

```
nvidia-broadcast.sh       launcher
nvidia-broadcast.desktop  application menu entry template (installed by deploy.sh)
scripts/build.sh          build all components with Zig
scripts/setup-prefix.sh   create prefix, run the installer, download models
scripts/build-ffmpeg.sh   FFmpeg-based parts for video export (libavformat with muxers, nvb_media.dll)
scripts/deploy.sh         install shims and patches into runner and prefix
scripts/patch-qcap.py     verified binary patches of Wine qcap
scripts/setup-vcam.sh     configure v4l2loopback (root)
src/nvcuda-shim           CUDA shim (cuGetProcAddress, GPU name, C4G table, bridge launcher, debug hook)
src/nvapi-shim            NVAPI shim
src/vad-replacement       virtual audio device library replacement
src/vcam-bridge           Wine-side bridge, Linux-side feeder, shared header
src/tools/v4lgrab.c       minimal V4L2 reader to test the virtual camera
src/media                 nvb_media.dll: AAC encoder MFT and mp4 sink writer on FFmpeg
src/probes                small Windows probes used while debugging
debug/                    gdb command file and a Chrome DevTools client for the app's UI
```

`NVB_DEV_EXEC=1` enables a developer hook in the nvcuda shim that runs commands written to `/tmp/nvb_exec.cmd` inside
the running Wine session (a second `umu-run` hangs while the app is open). Do not set it for normal use.

## License and third-party components

The code of this repository is released under the MIT license (`LICENSE`). This project is not affiliated with or
endorsed by NVIDIA. It contains no NVIDIA software: you need your own NVIDIA Broadcast installer (see "Gathering files"),
and the shims only reimplement the interfaces of the NVIDIA libraries.

- **FFmpeg** (LGPL 2.1 or later): not stored in this repository. `scripts/build-ffmpeg.sh` downloads the sources from
  ffmpeg.org and builds them on your machine, without `--enable-gpl` or `--enable-nonfree`. If you redistribute the
  resulting `libavformat.so.62` or `nvb_media.dll` (which links FFmpeg statically) you must comply with the LGPL: license
  text, the corresponding source and configure options, and a way to relink. AAC and H.264 may also be covered by patent
  licenses in your country; check before distributing binaries.
- **Wine / GE-Proton** (LGPL 2.1 or later): `patch-qcap.py` and `deploy.sh` modify a local copy and redistribute nothing.
