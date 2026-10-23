# Troubleshooting and fragile points

[Italiano](it/troubleshooting.md) | [Back to the README](../README.md)

## What can break

### After a driver update

The C4G offset in `src/nvcuda-shim/shim.c` can change with a new NVIDIA driver. The symptom is that video effects stop
working and the shim log (`/tmp/nvcuda_shim.log`) shows `C4G: no verified libcuda base`. Find the new offset of the
function whose first bytes are `55 31 c0 b9 1f 00 00 00 48 89 e5` in `libcuda.so.1`, then rebuild.

### After a GE-Proton update

The qcap patches are specific to GE-Proton11-11. An update of the runner replaces the patched files too. Run
`scripts/deploy.sh` again: it refuses unknown builds, and otherwise it installs the shims once more.

Video export has the same dependency. `deploy.sh` replaces GE's `libavformat.so.62.12.100` with the one built by
`build-ffmpeg.sh`, so a runner with a different FFmpeg version needs the matching FFmpeg source. The script checks the
versions and stops if they do not match.

### After a NVIDIA Broadcast update

The replacement of the audio library and the signature check on the core may break.

### With little GPU memory

The app and its effects need several hundred MiB of VRAM. With other GPU-heavy programs open, an `NV_ERR_NO_MEMORY` was seen
once in the kernel log.

## Problems and fixes

### Broadcast does not start

A previous session may still be running. Wine keeps its server alive as long as any process of the prefix runs, and a new
launch then waits without printing anything. Kill the old session:

```
WINEPREFIX=~/Games/umu/nvbroadcast/pfx ~/.local/share/Steam/compatibilitytools.d/GE-Proton11-11-x86_64/files/bin/wineserver -k
```

Do not use `pkill -f Broadcast.exe`: it also matches your own shell.

### An export stays "in progress"

This was a bug in Wine's source reader, now handled by the shim (see [how it works](how-it-works.md#reading-audio-and-video-together)).
If it happens again, note the time and look at the last lines of `/tmp/nvcuda_shim.log` that start with `srcreader:` or
`mp4sink:`.

### Log noise that is harmless

`[CRMAIN] Received empty frame from local processor with status: 5 | Dropping frame` appears constantly in the core log and
means nothing.

## Where the logs are

| What | Where |
|---|---|
| Shim log (CUDA, Media Foundation hooks, mp4 sink, AAC encoder) | `/tmp/nvcuda_shim.log` |
| Core and UI logs of the app | `<prefix>/pfx/drive_c/ProgramData/NVIDIA Corporation/NVIDIA Broadcast/logs/` |
| Virtual camera logs | the same folder, files named `NvBroadcastClientVCam_*` |

The core log of an export contains `MDFLIO` lines. The first error line after `ProcessMediaFile: starting` usually names the
failing step.

## Tools for developers

- `NVB_DEV_EXEC=1` enables a hook in the nvcuda shim that runs commands written to `/tmp/nvb_exec.cmd` inside the running
  Wine session. A second `umu-run` hangs while the app is open, which is why the hook exists. Do not set it for normal use.
- `src/probes` holds small Windows programs that reproduce one failure each. To run one inside the same container as the
  app, close the app first and use `umu-run probe.exe`. Probes that run directly with the Wine binary of the runner use
  the host's libraries and can behave differently.
- gdb works on the Wine process (`ptrace_scope=0`). Attach to one thread to see where a busy loop sits, for example
  `gdb -p <tid> -batch -ex "bt 12"`.
- `debug/` has a gdb command file and a Chrome DevTools client for the app's interface.
- `WINEDEBUG=err+all,+mfplat,+mfreadwrite` in front of `umu-run` shows what Wine's Media Foundation does.
