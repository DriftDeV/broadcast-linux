# License and third-party components

[Italiano](it/licenses.md) | [Back to the README](../README.md)

The code of this repository is released under the MIT license (see `LICENSE`).

This project is not affiliated with or endorsed by NVIDIA. It contains no NVIDIA software. You need your own NVIDIA
Broadcast installer (see [installation](install.md#getting-the-nvidia-files)), and the shims only reimplement the interfaces
of the NVIDIA libraries.

## FFmpeg

FFmpeg is under the LGPL 2.1 or later and is not stored in this repository. `scripts/build-ffmpeg.sh` downloads the sources
from ffmpeg.org and builds them on your machine, without `--enable-gpl` or `--enable-nonfree`.

If you redistribute the resulting `libavformat.so.62` or `nvb_media.dll` (which links FFmpeg statically), you must comply
with the LGPL: include the license text and the corresponding source with the configure options, and give users a way to
relink. AAC and H.264 may also be covered by patent licenses in your country. Check before distributing binaries.

## Wine and GE-Proton

Both are under the LGPL 2.1 or later. `patch-qcap.py` and `deploy.sh` modify a local copy and redistribute nothing.
