#!/bin/bash
# Configure v4l2loopback for the NVIDIA Broadcast virtual camera (run once, as root).
#
#   sudo scripts/setup-vcam.sh [--obs | --no-obs] [--nr N]
#
# The loopback device labelled "NVIDIA Broadcast" is found by the launcher through its label, so the number does not
# matter.  OBS Studio's Virtual Camera also uses v4l2loopback and takes the first free loopback device, so when OBS is in
# use this script creates a second, separate device labelled "OBS Virtual Camera" with a LOWER number: OBS picks it and
# never touches ours.  Both devices are described in one modprobe line, because options of several
# `options v4l2loopback` lines are not merged (the last value of a parameter wins): distro packages ship such a line
# for OBS (/usr/lib/modprobe.d/98-v4l2loopback.conf on Fedora), ours is named 99-* so it is read last and carries both.
set -eu

[ "$(id -u)" = 0 ] || { echo "run as root (sudo $0)" >&2; exit 1; }

nr=10
obs=auto
while [ $# -gt 0 ]; do
    case "$1" in
        --obs) obs=yes ;;
        --no-obs) obs=no ;;
        --nr) nr="$2"; shift ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
    shift
done

if [ "$obs" = auto ]; then
    obs=no
    if command -v obs >/dev/null 2>&1 || flatpak info com.obsproject.Studio >/dev/null 2>&1; then
        obs=yes
    fi
fi

if [ "$obs" = yes ]; then
    obs_nr=$((nr - 1))
    opts="devices=2 video_nr=$obs_nr,$nr card_label=\"OBS Virtual Camera,NVIDIA Broadcast\" exclusive_caps=1,1"
else
    opts="devices=1 video_nr=$nr card_label=\"NVIDIA Broadcast\" exclusive_caps=1"
fi

# files written by an earlier version of these instructions
rm -f /etc/modprobe.d/v4l2loopback.conf /etc/modules-load.d/v4l2loopback.conf
echo "options v4l2loopback $opts" > /etc/modprobe.d/99-nvidia-broadcast-vcam.conf
echo v4l2loopback > /etc/modules-load.d/99-nvidia-broadcast-vcam.conf

if lsmod | grep -q '^v4l2loopback '; then
    modprobe -r v4l2loopback || { echo "v4l2loopback is in use (OBS virtual camera / nvidia-broadcast running?); close them and rerun" >&2; exit 1; }
fi
modprobe v4l2loopback
for d in /sys/class/video4linux/video*; do
    case "$(cat "$d/name")" in
        "NVIDIA Broadcast"|"OBS Virtual Camera") echo "/dev/$(basename "$d"): $(cat "$d/name")" ;;
    esac
done
