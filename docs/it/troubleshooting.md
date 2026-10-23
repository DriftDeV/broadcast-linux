# Risoluzione dei problemi e punti fragili

[English](../troubleshooting.md) | [Torna al README](../../README.it.md)

## Cosa può rompersi

### Dopo un aggiornamento del driver

L'offset C4G in `src/nvcuda-shim/shim.c` può cambiare con un nuovo driver NVIDIA. Il sintomo è che gli effetti video
smettono di funzionare e il log della shim (`/tmp/nvcuda_shim.log`) mostra `C4G: no verified libcuda base`. Trova il nuovo
offset della funzione i cui primi byte sono `55 31 c0 b9 1f 00 00 00 48 89 e5` in `libcuda.so.1`, poi ricompila.

### Dopo un aggiornamento di GE-Proton

Le patch qcap sono specifiche per GE-Proton11-11. Un aggiornamento del runner sostituisce anche i file patchati. Riesegui
`scripts/deploy.sh`: rifiuta le build sconosciute, e per il resto reinstalla le shim.

L'export video ha la stessa dipendenza. `deploy.sh` sostituisce il `libavformat.so.62.12.100` di GE con quello compilato
da `build-ffmpeg.sh`, quindi un runner con un'altra versione di FFmpeg richiede il sorgente FFmpeg corrispondente. Lo
script controlla le versioni e si ferma se non coincidono.

### Dopo un aggiornamento di NVIDIA Broadcast

La sostituzione della libreria audio e il controllo della firma sul core possono rompersi.

### Con poca memoria GPU

L'app e i suoi effetti usano diverse centinaia di MiB di VRAM. Con altri programmi pesanti sulla GPU è comparso una volta
un `NV_ERR_NO_MEMORY` nel log del kernel.

## Problemi e rimedi

### Broadcast non si avvia

Può essere ancora attiva una sessione precedente. Wine tiene vivo il suo server finché gira un qualsiasi processo del
prefix, e un nuovo avvio resta in attesa senza stampare nulla. Termina la vecchia sessione:

```
WINEPREFIX=~/Games/umu/nvbroadcast/pfx ~/.local/share/Steam/compatibilitytools.d/GE-Proton11-11-x86_64/files/bin/wineserver -k
```

Non usare `pkill -f Broadcast.exe`: colpisce anche la tua shell.

### Un export resta "in elaborazione"

Era un bug del source reader di Wine, ora gestito dalla shim (vedi
[come funziona](how-it-works.md#lettura-insieme-di-audio-e-video)). Se capita di nuovo, annota l'ora e guarda le ultime
righe di `/tmp/nvcuda_shim.log` che iniziano con `srcreader:` o `mp4sink:`.

### Rumore innocuo nei log

`[CRMAIN] Received empty frame from local processor with status: 5 | Dropping frame` compare di continuo nel log del core
e non significa nulla.

## Dove sono i log

| Cosa | Dove |
|---|---|
| Log della shim (CUDA, hook Media Foundation, sink mp4, encoder AAC) | `/tmp/nvcuda_shim.log` |
| Log di core e UI dell'app | `<prefix>/pfx/drive_c/ProgramData/NVIDIA Corporation/NVIDIA Broadcast/logs/` |
| Log della camera virtuale | la stessa cartella, file chiamati `NvBroadcastClientVCam_*` |

Il log del core di un export contiene righe `MDFLIO`. La prima riga di errore dopo `ProcessMediaFile: starting` di solito
nomina il passaggio che fallisce.

## Strumenti per sviluppatori

- `NVB_DEV_EXEC=1` abilita nella shim nvcuda un hook che esegue i comandi scritti in `/tmp/nvb_exec.cmd` dentro la
  sessione Wine in corso. Un secondo `umu-run` si blocca mentre l'app è aperta, ed è per questo che l'hook esiste. Non
  impostarlo nell'uso normale.
- `src/probes` contiene piccoli programmi Windows che riproducono un guasto ciascuno. Per eseguirne uno nello stesso
  container dell'app, chiudi prima l'app e usa `umu-run probe.exe`. Le sonde lanciate direttamente con il binario Wine del
  runner usano le librerie dell'host e possono comportarsi in modo diverso.
- gdb funziona sul processo Wine (`ptrace_scope=0`). Collegati a un solo thread per vedere dove sta un ciclo che
  consuma CPU, per esempio `gdb -p <tid> -batch -ex "bt 12"`.
- `debug/` ha un file di comandi gdb e un client Chrome DevTools per l'interfaccia dell'app.
- `WINEDEBUG=err+all,+mfplat,+mfreadwrite` davanti a `umu-run` mostra cosa fa il Media Foundation di Wine.
