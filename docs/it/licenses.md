# Licenza e componenti di terzi

[English](../licenses.md) | [Torna al README](../../README.it.md)

Il codice di questo repository è rilasciato con licenza MIT (vedi `LICENSE`).

Il progetto non è affiliato né approvato da NVIDIA. Non contiene software NVIDIA. Serve il tuo installer di NVIDIA
Broadcast (vedi [installazione](install.md#ottenere-i-file-di-nvidia)) e le shim reimplementano soltanto le interfacce
delle librerie NVIDIA.

## FFmpeg

FFmpeg è sotto LGPL 2.1 o successiva e non è incluso in questo repository. `scripts/build-ffmpeg.sh` scarica i sorgenti
da ffmpeg.org e li compila sul tuo computer, senza `--enable-gpl` né `--enable-nonfree`.

Se ridistribuisci il `libavformat.so.62` o `nvb_media.dll` risultanti (che collega FFmpeg staticamente) devi rispettare
la LGPL: includere il testo della licenza e il sorgente corrispondente con le opzioni di configure, e dare agli utenti la
possibilità di rilinkare. AAC e H.264 possono inoltre richiedere licenze sui brevetti nel tuo paese. Verifica prima di
distribuire binari.

## Wine e GE-Proton

Entrambi sono sotto LGPL 2.1 o successiva. `patch-qcap.py` e `deploy.sh` modificano una copia locale e non ridistribuiscono
nulla.
