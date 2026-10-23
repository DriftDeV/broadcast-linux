#!/bin/bash
# Two things come from the FFmpeg 8.1 sources (downloaded from ffmpeg.org, nothing of FFmpeg is stored in this repo):
# 1. a libavformat that can write mp4/mov/wav, to replace the demux-only one bundled with GE-Proton;
# 2. nvb_media.dll (src/media): an AAC encoder MFT and an mp4 sink writer, linked statically against FFmpeg's native
#    AAC encoder and mp4 muxer (Wine has no AAC encoder and its mp4 sink is a stub).
# Details of (1):
# GE's FFmpeg is configured with --disable-everything and no muxers, so winedmo cannot create an mp4 sink
# ("Requested output format 'mp4' is not known"). Only libavformat is replaced: the version must match GE's
# libavcodec/libavutil exactly (FFmpeg 8.1: avformat 62.12.100, avcodec 62.28.100, avutil 60.26.100).
# Needs gcc, make, curl and zlib headers; no root, no nasm (x86 asm is not used by libavformat).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER=8.1
WORK="${NVB_FFMPEG_DIR:-$HOME/.cache/nvb-build/ffmpeg-src}"
OUT="$ROOT/build"
mkdir -p "$WORK" "$OUT"
cd "$WORK"
[ -f ffmpeg.tar.xz ] || curl -fsSL -o ffmpeg.tar.xz "https://ffmpeg.org/releases/ffmpeg-$VER.tar.xz"
[ -d "ffmpeg-$VER" ] || tar xf ffmpeg.tar.xz
for want in "LIBAVUTIL_VERSION_MINOR 26" "LIBAVCODEC_VERSION_MINOR 28" "LIBAVFORMAT_VERSION_MINOR 12"; do
    grep -Erq "define ${want% *}[[:space:]]+${want#* }\b" "ffmpeg-$VER"/lib*/version.h || { echo "FFmpeg source does not match GE-Proton's libs ($want)" >&2; exit 1; }
done
mkdir -p build && cd build
if [ ! -f config.h ]; then
    ../ffmpeg-$VER/configure --prefix="$WORK/dst" --enable-shared --disable-static --disable-everything --disable-programs \
        --disable-doc --disable-inline-asm --disable-x86asm --enable-demuxers --enable-muxers --enable-protocol=file \
        --enable-decoders --enable-encoders --enable-bsfs --enable-parsers --disable-decoder=vvc > configure.log 2>&1
fi
make -j"$(nproc)" libavformat/libavformat.so libavcodec/libavcodec.so libavutil/libavutil.so libswresample/libswresample.so > make.log 2>&1
install -m 755 libavformat/libavformat.so.62 "$OUT/libavformat.so.62"
strip --strip-unneeded "$OUT/libavformat.so.62"
echo "built $OUT/libavformat.so.62"

# 2. Windows static libavcodec/libavutil with only the AAC encoder, then the MFT DLL (cross-compiled with zig)
ZIG="${NVB_BUILD_VENV:-$HOME/.cache/nvb-build/venv}/bin/python -m ziglang"
cd "$WORK"; mkdir -p build-win; cd build-win
printf '#!/bin/bash\nargs=()\nfor a in "$@"; do case "$a" in -Wl,*) ;; *) args+=("$a");; esac; done\nexec %s cc -target x86_64-windows-gnu "${args[@]}"\n' "$ZIG" > zcc
printf '#!/bin/sh\nexec %s ar "$@"\n' "$ZIG" > zar
printf '#!/bin/sh\nexec %s ranlib "$@"\n' "$ZIG" > zranlib
printf '#!/bin/sh\nexec %s nm "$@"\n' "$ZIG" > znm
chmod +x zcc zar zranlib znm
if [ ! -f config-v2.stamp ]; then
    rm -f config.h
    ../ffmpeg-$VER/configure --enable-cross-compile --arch=x86_64 --target-os=mingw32 --cc="$PWD/zcc" --ar="$PWD/zar" \
        --ranlib="$PWD/zranlib" --nm="$PWD/znm" --disable-stripping --enable-static --disable-shared --disable-pic --disable-asm \
        --disable-everything --disable-programs --disable-doc --disable-avdevice --disable-avfilter \
        --disable-swscale --disable-swresample --disable-network --disable-autodetect --disable-debug \
        --enable-encoder=aac --enable-muxer=mp4 --enable-protocol=file > configure.log 2>&1
    touch config-v2.stamp
fi
make -j"$(nproc)" libavformat/libavformat.a libavcodec/libavcodec.a libavutil/libavutil.a > make.log 2>&1
$ZIG cc -target x86_64-windows-gnu -O2 -shared -I. -I"../ffmpeg-$VER" -o "$OUT/nvb_media.dll" "$ROOT/src/media/aacmft.c" "$ROOT/src/media/mp4sink.c" \
    libavformat/libavformat.a libavcodec/libavcodec.a libavutil/libavutil.a -lmfplat -lmfuuid -lole32 -luuid -lbcrypt
echo "built $OUT/nvb_media.dll"
