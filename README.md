# NVIDIA Broadcast on Linux

[Italiano](README.it.md)

This project runs the real Windows NVIDIA Broadcast 2.3.0 on Linux through Wine (GE-Proton), with NVIDIA's own AI
models. Your Linux applications see its output as a microphone, a speaker and a webcam.

![NVIDIA Broadcast running on Linux, Audio tab](docs/images/audio.png)

## What works

| Feature | Status |
|---|---|
| Microphone effects: noise removal, room echo, Studio Voice | Works. Appears as the PipeWire source "NVIDIA Broadcast". |
| Speaker effects: noise removal on incoming audio | Works. Appears as the PipeWire sink "NVIDIA Broadcast". |
| Camera effects: background blur or replacement, eye contact, auto frame | Works. Appears as the webcam "NVIDIA Broadcast" (v4l2loopback). |
| File processing: WAV, and MP4 with H.264 video and AAC audio | Works, tested with 720p and 1080p files. Other formats are untested. |

More pictures and some measurements are in [what it looks like](docs/demo.md).

## Before you start

- You need an NVIDIA RTX GPU with the proprietary driver, and your own copy of the NVIDIA Broadcast installer. This
  repository contains no NVIDIA software, and it is not affiliated with NVIDIA.
- It was developed and tested on one machine only: Fedora 44 (kernel 7.2), KDE Wayland, PipeWire, an RTX 4060 Laptop GPU
  with driver 615.78.08, Secure Boot on. On other systems expect to adapt a few things.
- The project is meant for personal use. It modifies a Wine runner and a Wine prefix on your machine, and it replaces some
  of NVIDIA's user-mode DLLs inside the prefix with compatible stand-ins.

## Quick start

```
git clone <this repo> && cd broadcast-linux
scripts/build.sh
scripts/build-ffmpeg.sh        # only if you want to process video files
scripts/setup-prefix.sh --installer ~/nvidia-files/broadcast-installer \
                        --nvngx-update ~/nvidia-files/driver/Display.Driver/nvngx_update.exe
scripts/deploy.sh
sudo scripts/setup-vcam.sh     # only for the webcam
nvidia-broadcast
```

The two NVIDIA files in the `setup-prefix.sh` line come from NVIDIA's downloads. The [installation guide](docs/install.md)
lists the packages to install first and shows how to get those files.

## Documentation

- [Installation](docs/install.md): requirements, getting the NVIDIA files, installing, starting.
- [What it looks like](docs/demo.md): screenshots and measurements.
- [How it works](docs/how-it-works.md): what was broken under Wine and how each piece was fixed.
- [Using it with OBS Studio](docs/obs.md)
- [Troubleshooting](docs/troubleshooting.md): what breaks after updates, where the logs are, tools for developers.
- [License and third-party components](docs/licenses.md)

## License

The code in this repository is under the MIT license, see [LICENSE](LICENSE). FFmpeg, Wine and GE-Proton are under the LGPL
and are not stored here, see the [notes on third-party components](docs/licenses.md).
