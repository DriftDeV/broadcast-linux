# Uso con OBS Studio

[English](../obs.md) | [Torna al README](../../README.it.md)

Anche la Virtual Camera di OBS usa v4l2loopback e prende il primo dispositivo loopback libero. Se trovasse quello usato da
questo progetto, ci scriverebbe dentro.

`setup-vcam.sh --obs` lo evita creando due dispositivi con una sola riga di opzioni del modulo in
`/etc/modprobe.d/99-nvidia-broadcast-vcam.conf`:

```
options v4l2loopback devices=2 video_nr=9,10 card_label="OBS Virtual Camera,NVIDIA Broadcast" exclusive_caps=1,1
```

`/dev/video9` è di OBS e ha il numero più basso. `/dev/video10` è di Broadcast, e il launcher lo trova dalla label.

## Note

- Tutto deve stare su una riga. Le opzioni di più righe `options v4l2loopback` non vengono unite, e vince l'ultimo valore
  di ogni parametro. Fedora ne installa una per OBS in `/usr/lib/modprobe.d/98-v4l2loopback.conf`. La nostra si chiama
  `99-*`, così viene letta per ultima.
- Una lista di label va tra virgolette come insieme (`"A,B"`). Mettere le virgolette attorno a ogni nome lascia
  caratteri di virgolette nei nomi.
- OBS non era installato sulla macchina di prova. La disposizione a due dispositivi, e il fatto che un lettore su
  `video9` non disturbi Broadcast, sono stati verificati con un piccolo lettore V4L2 (`src/tools/v4lgrab.c`) e non con
  OBS vero.
