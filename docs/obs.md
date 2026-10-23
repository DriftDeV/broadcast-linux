# Using it with OBS Studio

[Italiano](it/obs.md) | [Back to the README](../README.md)

OBS's Virtual Camera also uses v4l2loopback, and it takes the first free loopback device. If it found the one used by
this project, it would write into it.

`setup-vcam.sh --obs` avoids that by creating two devices with one module option line in
`/etc/modprobe.d/99-nvidia-broadcast-vcam.conf`:

```
options v4l2loopback devices=2 video_nr=9,10 card_label="OBS Virtual Camera,NVIDIA Broadcast" exclusive_caps=1,1
```

`/dev/video9` belongs to OBS and has the lower number. `/dev/video10` belongs to Broadcast, and the launcher finds it by
its label.

## Notes

- Everything must be on one line. The options of several `options v4l2loopback` lines are not merged, and the last value
  of a parameter wins. Fedora ships such a line for OBS in `/usr/lib/modprobe.d/98-v4l2loopback.conf`. Ours is named
  `99-*`, so it is read last.
- A list of labels is quoted as a whole (`"A,B"`). Quoting each name leaves stray quote characters in the names.
- OBS was not installed on the test machine. The two-device layout, and the fact that a reader on `video9` does not
  disturb Broadcast, were checked with a small V4L2 reader (`src/tools/v4lgrab.c`) and not with OBS itself.
