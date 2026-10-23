#!/bin/bash
# Launch NVIDIA Broadcast (Windows) under GE-Proton with the PipeWire devices it needs.
#   Microphone for Linux apps : "NVIDIA Broadcast"          (source nvbroadcast_mic)
#   Speaker for Linux apps    : "NVIDIA Broadcast"          (sink   nvbroadcast_speaker)
#   Internal mic input        : "NvB Mic Input" (sink  nvbroadcast_mic_in, written by the app)
set -u

ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
PREFIX="${NVB_PREFIX:-$HOME/Games/umu/nvbroadcast}"
RUNNER="${NVB_RUNNER:-$HOME/.local/share/Steam/compatibilitytools.d/GE-Proton11-11-x86_64}"
APP='C:\Program Files\NVIDIA Corporation\NVIDIA Broadcast\NVIDIA Broadcast.exe'

started=()
cleanup() { for p in "${started[@]}"; do kill "$p" 2>/dev/null; done; }
trap cleanup EXIT

have_node() { pactl list short sinks sources 2>/dev/null | awk '{print $2}' | grep -qx "$1"; }

if ! have_node nvbroadcast_mic; then
    pw-loopback -n nvb-mic -m '[ FL FR ]' \
        --capture-props='media.class=Audio/Sink node.name=nvbroadcast_mic_in node.description="NvB Mic Input"' \
        --playback-props='media.class=Audio/Source node.name=nvbroadcast_mic node.description="NVIDIA Broadcast"' &
    started+=($!)
fi
if ! have_node nvbroadcast_speaker; then
    pw-loopback -n nvb-spk -m '[ FL FR ]' \
        --capture-props='media.class=Audio/Sink node.name=nvbroadcast_speaker node.description="NVIDIA Broadcast"' \
        --playback-props='media.class=Audio/Source node.name=nvbroadcast_spk_tap node.description="Broadcast Spk Tap" node.passive=true' &
    started+=($!)
fi
# Virtual camera: needs a v4l2loopback device labelled "NVIDIA Broadcast" (see README). The feeder copies the frames the
# Wine bridge publishes into it, and only keeps the physical webcam busy while some application watches it.
VCAM_DEV=""
for d in /sys/class/video4linux/video*; do
    [ "$(cat "$d/name" 2>/dev/null)" = "NVIDIA Broadcast" ] && VCAM_DEV="/dev/$(basename "$d")"
done
if [ -n "$VCAM_DEV" ] && [ -x "$ROOT/build/nvb-vcam-feeder" ]; then
    "$ROOT/build/nvb-vcam-feeder" --device "$VCAM_DEV" &
    started+=($!)
    export NVB_VCAM_BRIDGE=1
else
    echo "virtual camera disabled (no v4l2loopback device \"NVIDIA Broadcast\" or feeder not built)" >&2
fi
sleep 1

export WINEPREFIX="$PREFIX" PROTONPATH="$RUNNER"
export PROTON_ENABLE_NVAPI=1 PROTON_NVIDIA_LIBS=1 PROTON_ENABLE_NGX_UPDATER=1
umu-run "$APP" "$@"
