# Come funziona

[English](../how-it-works.md) | [Torna al README](../../README.it.md)

L'app Windows così com'è non gira su Wine. Ogni sezione qui sotto era un guasto distinto da capire e correggere. La
libreria centrale dell'app, `NvBroadcastCore.dll`, non viene mai modificata perché verifica la propria firma. Tutto si fa
attorno a lei: DLL sostitutive, patch al runner Wine e hook che reindirizzano a runtime gli import del core.

In questa pagina: [installer](#installer), [CUDA](#cuda), [NVAPI](#nvapi), [modelli AI](#modelli-ai), [audio](#audio),
[ingresso webcam](#ingresso-webcam), [uscita webcam](#uscita-webcam), [elaborazione file](#elaborazione-file).

## Installer

`setup.exe` vuole `eula.txt` accanto a `setup.cfg`, la chiave di registro
`HKLM\Software\Microsoft\Windows\CurrentVersion\SharedDLLs` e la cartella `C:\ProgramData\NVIDIA\NGX\models`.

## CUDA

GE-Proton con `PROTON_NVIDIA_LIBS=1` fornisce `nvcuda.dll` come wrapper sopra la libcuda dell'host. Mancano tre cose,
quindi `src/nvcuda-shim` sostituisce il wrapper. L'originale resta come `nvcuda_real.dll`.

- `cuGetProcAddress` riconosceva solo le versioni "introdotta in" esatte. La shim restituisce la migliore versione non
  superiore a quella richiesta, come fa il driver vero.
- Il core confronta il nome della GPU riportato da CUDA ("... 4060 Laptop GPU") con quello di DXVK-NVAPI
  ("... 4060M") e rifiuta se differiscono. La shim riporta il nome NVAPI.
- Gli effetti video usano una tabella di export CUDA non documentata (UUID `d2688bf2-8371-e24f-9469-8177c8ccec27`, una
  funzione) che il wrapper Wine non ha. La shim chiama la funzione nativa nella `libcuda.so.1` dell'host tramite un
  thunk tra le convenzioni di chiamata Windows e SysV. La funzione si trova a un offset fisso dentro la libcuda di una
  specifica versione del driver (`C4G_OFFSET` in `shim.c`). Il suo indirizzo si ricava da `/proc/<pid>/maps` di un altro
  processo che ha libcuda caricata, e si verifica confrontando i primi 11 byte.

## NVAPI

`src/nvapi-shim` aggiunge `NvAPI_GPU_GetSerialNumber` (id `0x14B83A5F`) a DXVK-NVAPI. L'originale è conservato come
`nvapi64_real.dll`.

## Modelli AI

L'updater Linux `nvidia-ngx-updater` riceve errori HTTP dal server di NVIDIA. Il `nvngx_update.exe` per Windows funziona
sotto Wine con `PROTON_ENABLE_NGX_UPDATER=1`.

## Audio

Il driver audio virtuale di Windows (`nvrtxvad.sys`) non può esistere sotto Wine. La sua libreria user-mode
`nvrtxaudcap64v.dll` è sostituita da `src/vad-replacement`, che implementa la piccola tabella di funzioni chiamata dal
core: apri sessione, controlla l'endpoint predefinito, start e stop, invia un frame da 10 ms di audio a 48 kHz stereo
S16. Scrive i frame su un endpoint di rendering di Wine.

Il launcher crea dei loopback PipeWire in modo che questo endpoint diventi un microfono Linux. Il sink `NvB Mic Input` è
quello in cui scrive l'app, e la sorgente `NVIDIA Broadcast` è quella da cui registrano gli altri programmi. Per
l'altoparlante, il core cattura in loopback il sink `NVIDIA Broadcast`, lo elabora e lo riproduce sulla tua uscita reale.

## Ingresso webcam

`qcap` di Wine, il filtro DirectShow che cattura da V4L2, ha due lacune che il core non tollera.
`scripts/patch-qcap.py` applica queste patch al runner GE:

- `AvgTimePerFrame` non viene mai impostato e il core ne ricava il frame rate. La patch è in `qcap.so`.
- I frame non hanno timestamp e il core scarta ogni frame per cui `IMediaSample::GetTime` fallisce. La patch in
  `qcap.dll` imposta un timestamp relativo al primo frame. Con tempi assoluti il renderer si blocca.

Le patch usano spazi liberi riempiti di zeri e controllano prima i byte originali, quindi si applicano solo alla build
esatta per cui sono state scritte.

## Uscita webcam

Il core pubblica i frame elaborati al filtro DirectShow `NvVirtualCameraFilter_x64.dll` tramite memoria condivisa. Due
programmi portano i frame su Linux.

1. `src/vcam-bridge/bridge.c` è un programma Windows che la shim nvcuda avvia dentro il prefix quando `NVB_VCAM_BRIDGE=1`.
   Legge da quel filtro e scrive frame BGRA in `/dev/shm/nvb-vcam` (formato in `vcam_shm.h`). Collega direttamente
   camera, sample grabber e null renderer, perché `RenderStream` inserirebbe uno Smart Tee che va in deadlock. La shim
   lo esegue in un job object, così termina insieme all'app.
2. `src/vcam-bridge/feeder.c` gira su Linux. Converte in YUYV 1280x720 e scrive a 30 fps sul dispositivo v4l2loopback.
   Una volta al secondo controlla se un altro processo ha aperto il dispositivo loopback e lo comunica al bridge tramite
   l'header condiviso. Il bridge esegue il suo grafo, e quindi tiene accesa la webcam vera, solo mentre qualcuno guarda
   quella virtuale. Se per 1,5 secondi non arriva un frame recente, invia nero.

## Elaborazione file

La scheda File ha bisogno di diverse cose che a Wine mancano. Sono tutte fornite senza toccare `NvBroadcastCore.dll`. La
shim nvcuda reindirizza a runtime gli import del core da `mfreadwrite.dll` e `mfplat.dll`
(`src/nvcuda-shim/mfhooks.c`).

### Output WAV

Il sink writer di Wine non sa creare un `.wav`: `MFCreateSinkWriterFromURL` fallisce con `0x80004003` e il job termina con
errore -9111. La shim fornisce un piccolo `IMFSinkWriter` WAV.

### Input video

Wine elenca il formato decodificato (NV12) prima di quello compresso (H264) come tipo nativo di uno stream video, e il
core legge il tipo 0 come codec, quindi ogni video veniva rifiutato con "Source video codec is not supported". La shim
riordina i tipi.

### Lettura insieme di audio e video

Il source reader di Wine perde dati con `ReadSample(ANY_STREAM)`. Su un file da 15 secondi perdeva l'ultimo terzo
dell'audio. Poi risponde a ogni lettura successiva con un fine-flusso che il core non accetta mai, e l'export resta "in
elaborazione" per sempre.

La shim emula `ANY_STREAM` con letture esplicite dello stream più indietro, che non perdono nulla. Riporta ogni
fine-flusso con il suo indice. Trattiene l'ultimo finché l'encoder AAC è inattivo da un secondo, così il core non ferma
i suoi stadi audio mentre ci sono ancora dati in coda.

### Output MP4

Mancano tre cose.

- Il `libavformat` incluso in GE-Proton non ha muxer (`Requested output format 'mp4' is not known`).
- Wine non ha un encoder AAC (`No AAC encoder MFT found`).
- Il media sink MP4 di Wine è un abbozzo: `BeginWriting` fallisce con `E_NOTIMPL`.

`scripts/build-ffmpeg.sh` scarica FFmpeg 8.1 e ne compila due cose. La prima è un `libavformat` con i muxer, che
`deploy.sh` installa al posto di quello di GE. La seconda è `nvb_media.dll` (`src/media`), che contiene una transform
AAC basata sull'encoder AAC nativo di FFmpeg e un sink writer mp4 basato sul muxer mp4 di FFmpeg. La shim registra
l'encoder con `MFTRegisterLocal`, aggiunge `MFT_ENUM_FLAG_LOCALMFT` alla ricerca degli encoder audio del core (il core non
chiede le transform locali) e instrada gli output `.mp4`, `.m4a` e `.mov` al sink writer. Il video lo codifica il core
stesso con NVENC.

## Struttura del repository

```
nvidia-broadcast.sh       launcher
nvidia-broadcast.desktop  modello della voce di menu (installato da deploy.sh)
scripts/build.sh          compila tutti i componenti con Zig
scripts/setup-prefix.sh   crea il prefix, esegue l'installer, scarica i modelli
scripts/build-ffmpeg.sh   parti basate su FFmpeg per l'export video (libavformat con muxer, nvb_media.dll)
scripts/deploy.sh         installa shim e patch in runner e prefix
scripts/patch-qcap.py     patch binarie verificate di qcap di Wine
scripts/setup-vcam.sh     configura v4l2loopback (root)
src/nvcuda-shim           shim CUDA (cuGetProcAddress, nome GPU, tabella C4G), hook Media Foundation (mfhooks.c), avvio bridge, hook di debug
src/nvapi-shim            shim NVAPI
src/vad-replacement       sostituzione della libreria del dispositivo audio virtuale
src/vcam-bridge           bridge lato Wine, feeder lato Linux, header condiviso
src/media                 nvb_media.dll: MFT encoder AAC e sink writer mp4 su FFmpeg
src/tools/v4lgrab.c       lettore V4L2 minimale per provare la camera virtuale
src/probes                piccole sonde Windows usate nel debug
debug/                    file di comandi gdb e client Chrome DevTools per la UI dell'app
docs/                     questa documentazione
```
