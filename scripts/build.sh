#!/bin/bash
# Build every component of the project with Zig (installed in a private venv, no root needed).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENV="${NVB_BUILD_VENV:-$HOME/.cache/nvb-build/venv}"
OUT="$ROOT/build"

if [ ! -x "$VENV/bin/python" ]; then
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install -q ziglang
fi
ZIG=("$VENV/bin/python" -m ziglang cc)
WIN=(-target x86_64-windows-gnu -O2)
mkdir -p "$OUT"

echo "[1/5] nvcuda shim"
"${ZIG[@]}" "${WIN[@]}" -shared -o "$OUT/nvcuda.dll" "$ROOT/src/nvcuda-shim/shim.c" "$ROOT/src/nvcuda-shim/mfhooks.c" "$ROOT/src/nvcuda-shim/nvcuda.def" -lmfuuid -lole32 -luuid
echo "[2/5] nvapi shim"
"${ZIG[@]}" "${WIN[@]}" -shared -o "$OUT/nvapi64.dll" "$ROOT/src/nvapi-shim/nvapi_shim.c"
echo "[3/5] virtual audio device replacement"
"${ZIG[@]}" "${WIN[@]}" -shared -o "$OUT/nvrtxaudcap64v.dll" "$ROOT/src/vad-replacement/vadstub.c" -lole32 -luuid -loleaut32
echo "[4/5] virtual camera bridge (Wine side)"
"${ZIG[@]}" "${WIN[@]}" -o "$OUT/nvb-vcam-bridge.exe" "$ROOT/src/vcam-bridge/bridge.c" -lstrmiids -lole32 -loleaut32 -luuid
echo "[5/5] virtual camera feeder (Linux side)"
"${ZIG[@]}" -O2 -Wall -o "$OUT/nvb-vcam-feeder" "$ROOT/src/vcam-bridge/feeder.c"
echo "done: $OUT"
ls -la "$OUT"
