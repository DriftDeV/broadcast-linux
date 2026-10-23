# How it works

[Italiano](it/how-it-works.md) | [Back to the README](../README.md)

The Windows app does not run on Wine as it is. Each section below was a separate failure that had to be understood and
fixed. The core library of the app, `NvBroadcastCore.dll`, is never modified because it verifies its own signature.
Everything is done around it: replacement DLLs, patches to the Wine runner, and hooks that redirect the core's imports
at run time.

On this page: [installer](#installer), [CUDA](#cuda), [NVAPI](#nvapi), [AI models](#ai-models), [audio](#audio),
[webcam input](#webcam-input), [webcam output](#webcam-output), [file processing](#file-processing).

## Installer

`setup.exe` needs `eula.txt` next to `setup.cfg`, the registry key
`HKLM\Software\Microsoft\Windows\CurrentVersion\SharedDLLs`, and the directory `C:\ProgramData\NVIDIA\NGX\models`.

## CUDA

GE-Proton with `PROTON_NVIDIA_LIBS=1` provides `nvcuda.dll` as a wrapper over the host's libcuda. Three things are
missing, so `src/nvcuda-shim` replaces the wrapper. The original stays as `nvcuda_real.dll`.

- `cuGetProcAddress` only matched exact "introduced in" versions. The shim returns the best version not above the one
  requested, as the real driver does.
- The core compares the GPU name reported by CUDA ("... 4060 Laptop GPU") with the one from DXVK-NVAPI ("... 4060M") and
  rejects a mismatch. The shim reports the NVAPI name.
- Video effects use an undocumented CUDA export table (UUID `d2688bf2-8371-e24f-9469-8177c8ccec27`, one function) that the
  Wine wrapper lacks. The shim calls the native function in the host's `libcuda.so.1` through a thunk between the
  Windows and SysV calling conventions. The function sits at a fixed offset inside the libcuda of one driver version
  (`C4G_OFFSET` in `shim.c`). Its address is found through `/proc/<pid>/maps` of another process that has libcuda
  loaded, and it is verified by comparing its first 11 bytes.

## NVAPI

`src/nvapi-shim` adds `NvAPI_GPU_GetSerialNumber` (id `0x14B83A5F`) to DXVK-NVAPI. The original is kept as
`nvapi64_real.dll`.

## AI models

The Linux `nvidia-ngx-updater` gets HTTP errors from NVIDIA's server. The Windows `nvngx_update.exe` works under Wine with
`PROTON_ENABLE_NGX_UPDATER=1`.

## Audio

The Windows virtual audio driver (`nvrtxvad.sys`) cannot exist under Wine. Its user-mode library `nvrtxaudcap64v.dll` is
replaced by `src/vad-replacement`, which implements the small function table the core calls: open a session, check the
default endpoint, start and stop, and push a 10 ms frame of 48 kHz stereo S16 audio. It writes the frames to a Wine
render endpoint.

The launcher creates PipeWire loopbacks so that this endpoint becomes a Linux microphone. The sink `NvB Mic Input` is
what the app writes into, and the source `NVIDIA Broadcast` is what other programs record from. For the speaker, the
core captures the sink `NVIDIA Broadcast` in loopback, processes it, and plays it on your real output.

## Webcam input

Wine's `qcap`, the DirectShow filter that captures from V4L2, has two gaps that the core does not tolerate.
`scripts/patch-qcap.py` patches the GE runner:

- `AvgTimePerFrame` is never set, and the core derives the frame rate from it. The patch is in `qcap.so`.
- Frames carry no time stamp, and the core drops every frame for which `IMediaSample::GetTime` fails. The patch in
  `qcap.dll` sets a time stamp relative to the first frame. Absolute times make the renderer block.

The patches use code caves in zero padding and check the original bytes first, so they apply only to the exact build they
were written for.

## Webcam output

The core publishes processed frames to the DirectShow filter `NvVirtualCameraFilter_x64.dll` through shared memory. Two
programs carry the frames to Linux.

1. `src/vcam-bridge/bridge.c` is a Windows program that the nvcuda shim starts inside the prefix when `NVB_VCAM_BRIDGE=1`.
   It reads from that filter and writes BGRA frames to `/dev/shm/nvb-vcam` (layout in `vcam_shm.h`). It connects the
   camera, the sample grabber and the null renderer directly, because `RenderStream` would insert a Smart Tee that
   deadlocks. The shim runs it in a job object, so it ends together with the app.
2. `src/vcam-bridge/feeder.c` runs on Linux. It converts to 1280x720 YUYV and writes 30 fps to the v4l2loopback
   device. Once per second it checks whether any other process has the loopback device open and tells the bridge
   through the shared header. The bridge runs its graph, and so keeps the real webcam on, only while somebody watches
   the virtual one. When no fresh frame arrives for 1.5 seconds it sends black.

## File processing

The Files tab needs several things that Wine lacks. All of them are supplied without touching `NvBroadcastCore.dll`. The
nvcuda shim redirects the core's imports from `mfreadwrite.dll` and `mfplat.dll` at run time
(`src/nvcuda-shim/mfhooks.c`).

### WAV output

Wine's sink writer cannot create a `.wav`: `MFCreateSinkWriterFromURL` fails with `0x80004003` and the job ends with error
-9111. The shim provides a small WAV `IMFSinkWriter`.

### Video input

Wine lists the decoded format (NV12) before the compressed one (H264) as the native type of a video stream, and the core
reads type 0 as the codec, so every video was refused with "Source video codec is not supported". The shim reorders the
types.

### Reading audio and video together

Wine's source reader loses data on `ReadSample(ANY_STREAM)`. On a 15 second file it dropped the last third of the audio.
It then answers every further read with an end of stream that the core never accepts, and the export stays "in progress"
forever.

The shim emulates `ANY_STREAM` with explicit reads of the stream that is furthest behind, which is lossless. It reports
each end of stream with its own index. It holds the last one back until the AAC encoder has been idle for a second, so
that the core does not stop its audio stages while data is still queued.

### MP4 output

Three things are missing here.

- GE-Proton's bundled `libavformat` has no muxers (`Requested output format 'mp4' is not known`).
- Wine has no AAC encoder (`No AAC encoder MFT found`).
- Wine's MP4 media sink is a stub: `BeginWriting` fails with `E_NOTIMPL`.

`scripts/build-ffmpeg.sh` downloads FFmpeg 8.1 and builds two things from it. The first is a `libavformat` with muxers that
`deploy.sh` installs in place of GE's copy. The second is `nvb_media.dll` (`src/media`), which holds an AAC encoder
transform on FFmpeg's native AAC encoder and an mp4 sink writer on FFmpeg's mp4 muxer. The shim registers the encoder
with `MFTRegisterLocal`, adds `MFT_ENUM_FLAG_LOCALMFT` to the core's audio encoder lookup (the core does not ask for local
transforms), and routes `.mp4`, `.m4a` and `.mov` outputs to the sink writer. The video is encoded by the core itself
with NVENC.

## Repository layout

```
nvidia-broadcast.sh       launcher
nvidia-broadcast.desktop  application menu entry template (installed by deploy.sh)
scripts/build.sh          build all components with Zig
scripts/setup-prefix.sh   create the prefix, run the installer, download the models
scripts/build-ffmpeg.sh   FFmpeg-based parts for video export (libavformat with muxers, nvb_media.dll)
scripts/deploy.sh         install shims and patches into the runner and the prefix
scripts/patch-qcap.py     verified binary patches of Wine's qcap
scripts/setup-vcam.sh     configure v4l2loopback (root)
src/nvcuda-shim           CUDA shim (cuGetProcAddress, GPU name, C4G table), Media Foundation hooks (mfhooks.c), bridge launcher, debug hook
src/nvapi-shim            NVAPI shim
src/vad-replacement       replacement for the virtual audio device library
src/vcam-bridge           Wine-side bridge, Linux-side feeder, shared header
src/media                 nvb_media.dll: AAC encoder MFT and mp4 sink writer on FFmpeg
src/tools/v4lgrab.c       minimal V4L2 reader to test the virtual camera
src/probes                small Windows probes used while debugging
debug/                    gdb command file and a Chrome DevTools client for the app's UI
docs/                     this documentation
```
