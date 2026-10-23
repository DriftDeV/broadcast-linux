#!/bin/bash
# Install the built components into the GE-Proton runner and the Wine prefix (idempotent, keeps *.orig backups).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUNNER="${NVB_RUNNER:-$HOME/.local/share/Steam/compatibilitytools.d/GE-Proton11-11-x86_64}"
PREFIX="${NVB_PREFIX:-$HOME/Games/umu/nvbroadcast}"
SYS32="$PREFIX/pfx/drive_c/windows/system32"
B="$ROOT/build"
[ -f "$B/nvcuda.dll" ] || { echo "run scripts/build.sh first" >&2; exit 1; }

keep_orig() { [ -e "$1.orig" ] || cp -a "$1" "$1.orig"; chmod u+w "$1"; }

# nvcuda: Proton copies this file to system32 at every start; the real wrapper becomes nvcuda_real.dll
CUDA_SRC="$RUNNER/files/lib/wine/nvidia-libs/nvcuda/x86_64-windows/nvcuda.dll.so"
keep_orig "$CUDA_SRC"
install -m 755 "$CUDA_SRC.orig" "$SYS32/nvcuda_real.dll"
install -m 755 "$B/nvcuda.dll" "$CUDA_SRC"

# nvapi (DXVK-NVAPI): same scheme
NVAPI_SRC="$RUNNER/files/lib/wine/nvapi/x86_64-windows/nvapi64.dll"
keep_orig "$NVAPI_SRC"
install -m 755 "$NVAPI_SRC.orig" "$SYS32/nvapi64_real.dll"
install -m 755 "$B/nvapi64.dll" "$NVAPI_SRC"

# virtual audio device user-mode library (installed by the NVIDIA setup into system32)
install -m 755 "$B/nvrtxaudcap64v.dll" "$SYS32/nvrtxaudcap64v.dll"

# virtual camera bridge (Wine side) and feeder (Linux side)
install -m 755 "$B/nvb-vcam-bridge.exe" "$PREFIX/pfx/drive_c/nvb-vcam-bridge.exe"
mkdir -p "$HOME/.local/bin"
ln -sf "$ROOT/nvidia-broadcast.sh" "$HOME/.local/bin/nvidia-broadcast"

# application menu entry
mkdir -p "$HOME/.local/share/applications"
# icon: the one Proton extracted from the app into the prefix, else a generic theme icon
ICON="$PREFIX/pfx/drive_c/proton_shortcuts/icons/256x256/apps/C74F_NVIDIA Broadcast.0.png"
[ -f "$ICON" ] || ICON=camera-video
sed -e "s#@ROOT@#$ROOT#" -e "s#@ICON@#$ICON#" "$ROOT/nvidia-broadcast.desktop" > "$HOME/.local/share/applications/nvidia-broadcast.desktop"

# AAC encoder + mp4 sink writer (optional: scripts/build-ffmpeg.sh); the shim loads it from system32
rm -f "$SYS32/nvb_aac.dll"
[ -f "$B/nvb_media.dll" ] && install -m 755 "$B/nvb_media.dll" "$SYS32/nvb_media.dll"

# libavformat with mp4/mov/wav muxers (optional: needs scripts/build-ffmpeg.sh); replaces GE's demux-only copy
if [ -f "$B/libavformat.so.62" ]; then
    AVF="$RUNNER/files/lib/x86_64-linux-gnu/libavformat.so.62.12.100"
    keep_orig "$AVF"
    install -m 755 "$B/libavformat.so.62" "$AVF"
else
    echo "note: build/libavformat.so.62 missing, video export will fail (run scripts/build-ffmpeg.sh)"
fi

python3 "$ROOT/scripts/patch-qcap.py"
echo "deployed."
