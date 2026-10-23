# Come si presenta

[English](../demo.md) | [Torna al README](../../README.it.md)

Questi sono screenshot del vero NVIDIA Broadcast 2.3.0 in esecuzione sotto Wine su Fedora 44 (KDE Wayland, RTX 4060 Laptop
GPU). L'interfaccia è in italiano perché lo è la lingua del sistema.

## Audio

![La scheda Audio con Voce in studio attiva e un campione di prova registrato](../images/audio.png)

La Voce in studio è attiva per il microfono. Il pannello di prova a destra ha registrato un campione: la forma d'onda in
alto è l'ingresso e quella verde sotto è l'uscita elaborata. Il microfono appare alle applicazioni Linux come sorgente
PipeWire "NVIDIA Broadcast".

Con la Voce in studio in tempo reale l'uso è stato di circa il 16-24% della GPU (9-13% della banda di memoria), intorno a
1,1 GiB di VRAM e 14 W, con la GPU a 57 °C. Encoder e decoder video sono rimasti allo 0%.

## Video

![La scheda Video con rimozione del disturbo, Super Resolution e Frame Generation](../images/video.png)

La scheda Video elenca gli effetti della camera. La webcam è l'ingresso, e l'immagine elaborata è quella che le
applicazioni vedono come webcam "NVIDIA Broadcast" (un dispositivo v4l2loopback).

## Elaborazione file

![L'elaboratore file con un export completato e il video di output in riproduzione](../images/files.png)

La scheda File elabora file audio e video. Questo export è stato fatto su un mp4 1080p a 30 fps di 15 secondi, con
sfocatura dello sfondo, rimozione del disturbo video e Voce in studio. Il risultato è un mp4 con video H.264 e audio AAC.
Aveva gli stessi 467 fotogrammi dell'originale e l'audio completo, ed è stato prodotto in 10-11 secondi su questa GPU. Una
clip da 5 secondi a 720p ha richiesto meno di 3 secondi. Funzionano anche i file WAV.

## Impostazioni

![La scheda Impostazioni con la versione 2.3.0](../images/settings.png)

La scheda Impostazioni conferma la versione in esecuzione: NVIDIA Broadcast 2.3.0.
