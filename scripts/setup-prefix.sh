#!/bin/bash
# Create the Wine prefix and install NVIDIA Broadcast into it with the stock Windows installer.
#
#   scripts/setup-prefix.sh --installer DIR --nvngx-update FILE
#
#   --installer DIR        the extracted NVIDIA Broadcast installer (the folder that holds setup.exe and setup.cfg)
#   --nvngx-update FILE    nvngx_update.exe from a Windows NVIDIA driver package (Display.Driver/, extract with 7z).
#                          The Linux nvidia-ngx-updater cannot be used: the model server rejects its platform.
#
# Neither file is part of this repository; you must have them under your own licence terms from NVIDIA.
# Run scripts/build.sh and scripts/deploy.sh afterwards.
set -euo pipefail

RUNNER="${NVB_RUNNER:-$HOME/.local/share/Steam/compatibilitytools.d/GE-Proton11-11-x86_64}"
PREFIX="${NVB_PREFIX:-$HOME/Games/umu/nvbroadcast}"
installer=""; nvngx=""
while [ $# -gt 0 ]; do
    case "$1" in
        --installer) installer="$2"; shift ;;
        --nvngx-update) nvngx="$2"; shift ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
    shift
done
[ -f "$installer/setup.exe" ] || { echo "--installer must point to the folder with setup.exe" >&2; exit 1; }
[ -f "$nvngx" ] || { echo "--nvngx-update must point to nvngx_update.exe" >&2; exit 1; }
[ -d "$RUNNER" ] || { echo "runner not found: $RUNNER (install GE-Proton11-11, see README)" >&2; exit 1; }
command -v umu-run >/dev/null || { echo "umu-run not found (install umu-launcher)" >&2; exit 1; }

export WINEPREFIX="$PREFIX" PROTONPATH="$RUNNER" PROTON_ENABLE_NVAPI=1 PROTON_NVIDIA_LIBS=1 WINEDEBUG=-all
mkdir -p "$PREFIX"
C="$PREFIX/pfx/drive_c"

echo "[1/5] creating the prefix and the SharedDLLs registry key (the installer fails without it)"
K='HKLM\Software\Microsoft\Windows\CurrentVersion\SharedDLLs'
umu-run reg add "$K" /f /reg:64 >/dev/null
umu-run reg add "$K" /f /reg:32 >/dev/null

echo "[2/5] directories the installer and the model updater expect"
mkdir -p "$C/ProgramData/NVIDIA/NGX/models" "$C/ProgramData/NVIDIA Corporation/NVIDIA Broadcast/logs"

echo "[3/5] running setup.exe (it needs eula.txt next to setup.cfg)"
[ -f "$installer/eula.txt" ] || cp "$installer/NvMaxineClient/EULA.txt" "$installer/eula.txt"
( cd "$installer" && umu-run ./setup.exe )

echo "[4/5] Windows model updater"
install -m 755 "$nvngx" "$C/windows/system32/nvngx_update.exe"

echo "[5/5] downloading the AI models (a few hundred MB)"
PROTON_ENABLE_NGX_UPDATER=1 umu-run 'C:\Program Files\NVIDIA Corporation\NVIDIA Broadcast\NvBroadcastInstallerOTAUtility.exe' --install
ls "$C/ProgramData/NVIDIA/NGX/models"
echo "prefix ready: $PREFIX"
