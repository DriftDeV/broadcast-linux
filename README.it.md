# NVIDIA Broadcast su Linux (Wine / GE-Proton)

[English version](README.md)

Esegue il vero **NVIDIA Broadcast 2.3.0** per Windows su Linux, con i modelli Maxine originali, e rende il risultato
disponibile alle applicazioni Linux come microfono, altoparlante e webcam:

| Funzione | Stato |
|---|---|
| Effetti microfono (rimozione rumore, eco della stanza) | funziona, appare come sorgente PipeWire "NVIDIA Broadcast" |
| Effetti altoparlante (rimozione rumore dell'audio in ingresso) | funziona, appare come sink PipeWire "NVIDIA Broadcast" |
| Effetti camera (sfocatura/sostituzione sfondo, contatto visivo, inquadratura automatica) | funziona, appare come webcam "NVIDIA Broadcast" (v4l2loopback) |
| Elaborazione di file audio/video | non testata |

Questo repository contiene **solo i miei sorgenti e script**. Non contiene binari, installer, modelli o driver NVIDIA,
e non è affiliato, approvato o supportato da NVIDIA. Servono una tua copia dell'installer di NVIDIA Broadcast e di un
pacchetto driver NVIDIA per Windows, secondo i termini di NVIDIA. Il progetto è pensato per uso personale: modifica un
runner Wine e un prefix Wine sulla tua macchina e sostituisce alcune DLL user-mode di NVIDIA nel prefix con sostituti
compatibili (vedi [Come funziona](#come-funziona)).

È stato sviluppato e provato su una sola macchina: Fedora 44 (kernel 7.2), KDE Wayland, PipeWire, RTX 4060 Laptop GPU
con driver proprietario 615.78.08, Secure Boot attivo. Su altri sistemi aspettati di dover adattare qualcosa (vedi
[Punti fragili](#punti-fragili)).

## Requisiti

- Una GPU NVIDIA RTX con driver proprietario (sull'host devono esserci le librerie CUDA e NGX).
- `umu-launcher` (`umu-run`), PipeWire con `pw-loopback`, `python3` con `venv`, `p7zip` (per estrarre il driver Windows).
- **GE-Proton11-11** (build x86_64) estratto in `~/.local/share/Steam/compatibilitytools.d/GE-Proton11-11-x86_64`.
  UMU-Proton non funziona: il suo `nvcuda.dll` è vuoto. Con altre build di GE le patch binarie di
  `scripts/patch-qcap.py` vanno ricavate di nuovo, e lo script rifiuta di applicarle alla cieca.
- `v4l2loopback` per la webcam. Su Fedora: `sudo dnf install akmod-v4l2loopback v4l2loopback` (RPM Fusion). Con
  Secure Boot il modulo dell'akmod deve essere firmato con una chiave MOK registrata.
- L'installer Windows di NVIDIA Broadcast (estratto, una cartella con `setup.exe` e `setup.cfg`) e `nvngx_update.exe`
  da un pacchetto driver NVIDIA per Windows (`Display.Driver/` dentro il driver estratto; è stata usata la versione
  616.64).

## Installare i requisiti

Solo la colonna Fedora è stata provata. I nomi dei pacchetti per Debian/Ubuntu e Arch seguono la nomenclatura abituale
delle distribuzioni e possono differire nella tua versione: se uno non si trova, controlla con il gestore pacchetti.

**Fedora** (con [RPM Fusion](https://rpmfusion.org/Configuration) attivo per il driver NVIDIA e v4l2loopback):

```
sudo dnf install python3 pipewire-utils 7zip umu-launcher
sudo dnf install akmod-nvidia xorg-x11-drv-nvidia-cuda akmod-v4l2loopback v4l2loopback
```

**Debian / Ubuntu**:

```
sudo apt install python3 python3-venv pipewire-bin 7zip dkms linux-headers-$(uname -r) v4l2loopback-dkms
# Driver NVIDIA con CUDA: Ubuntu: sudo ubuntu-drivers install   Debian: sudo apt install nvidia-driver libcuda1
```

`7zip` si chiama `p7zip-full` nelle versioni più vecchie. `umu-launcher` non ha un pacchetto apt: installa i file `.deb`
dalle [release di umu-launcher](https://github.com/Open-Wine-Components/umu-launcher/releases). Con Secure Boot, DKMS
chiede di registrare una chiave MOK la prima volta (`mokutil`), poi riavvia e conferma.

**Arch**:

```
sudo pacman -S python pipewire 7zip dkms linux-headers nvidia-utils v4l2loopback-dkms
yay -S umu-launcher            # AUR, oppure pacman -S umu-launcher se i tuoi repository lo hanno
```

Per il driver vero e proprio usa `nvidia-open-dkms` o `nvidia-dkms` (o `nvidia`/`nvidia-open` per il kernel standard).
Con Secure Boot i moduli DKMS vanno firmati a mano.

**GE-Proton11-11** (tutte le distribuzioni): scarica il tarball x86_64 dalle
[release di GE-Proton](https://github.com/GloriousEggroll/proton-ge-custom/releases) ed estrailo:

```
mkdir -p ~/.local/share/Steam/compatibilitytools.d
tar -xf GE-Proton11-11*.tar.* -C ~/.local/share/Steam/compatibilitytools.d
ls ~/.local/share/Steam/compatibilitytools.d     # se il nome cartella differisce da GE-Proton11-11-x86_64, imposta NVB_RUNNER
```

**Controllo lato NVIDIA**: Proton ha bisogno delle librerie lato Windows del driver, che i pacchetti del driver
proprietario installano. Questo comando deve elencare `nvngx.dll`:

```
ls /usr/lib*/nvidia/wine/nvngx.dll
```

## Reperire i file

Servono due file di NVIDIA. Non sono in questo repository e li scarichi tu, secondo i termini di NVIDIA. Tienili
fuori dal repository, per esempio in `~/nvidia-files`:

```
mkdir -p ~/nvidia-files
```

**1. L'installer di NVIDIA Broadcast** (`NVIDIA_Broadcast_v2.3.0.<build>.exe`, circa 190 MB). Scaricalo dalla
[pagina di NVIDIA Broadcast](https://www.nvidia.com/en-us/geforce/broadcasting/broadcast-app/) e mettilo in
`~/nvidia-files`. È un archivio autoestraente che si espande a circa 730 MB; estrailo con 7-Zip invece di
eseguirlo:

```
7z x ~/nvidia-files/NVIDIA_Broadcast_v2.3.0.*.exe -o$HOME/nvidia-files/broadcast-installer
ls ~/nvidia-files/broadcast-installer        # deve contenere setup.exe, setup.cfg, NvMaxineClient, NvMaxineModels ...
```

`broadcast-installer` è la cartella da passare come `--installer`. La versione provata è la 2.3.0.

**2. `nvngx_update.exe`** (updater dei modelli, circa 1 MB) dal pacchetto driver NVIDIA per **Windows**. Nella
[pagina di download dei driver NVIDIA](https://www.nvidia.com/Download/index.aspx) scegli Windows 10/11 64-bit e la tua
GPU, scarica il `.exe` in `~/nvidia-files` (la versione provata è la 616.64) ed estrai solo quel file:

```
7z x ~/nvidia-files/616.64-*.exe -o$HOME/nvidia-files/driver Display.Driver/nvngx_update.exe
ls ~/nvidia-files/driver/Display.Driver/nvngx_update.exe
```

Anche l'installer del driver è un archivio 7-Zip, quindi `7z` lo apre senza Windows. Se nella tua versione del driver il
file non è in quel percorso, cercalo nell'elenco: `7z l driver.exe | grep -i ngx`. Il percorso del file estratto è
quello da passare come `--nvngx-update`. Il `nvidia-ngx-updater` per Linux non lo sostituisce: il server dei modelli di
NVIDIA ne rifiuta la piattaforma.

A installazione finita puoi cancellare la cartella da 730 MB e i `.exe` scaricati: servono solo a `setup-prefix.sh`.

## Installazione

```
git clone <questo repo> && cd broadcast-linux

scripts/build.sh                      # compila tutto in build/ (installa Zig in ~/.cache/nvb-build, senza root)
scripts/build-ffmpeg.sh               # facoltativo, solo per la scheda File con i video: scarica FFmpeg 8.1 e compila due parti
scripts/setup-prefix.sh --installer ~/nvidia-files/broadcast-installer \
                        --nvngx-update ~/nvidia-files/driver/Display.Driver/nvngx_update.exe
scripts/deploy.sh                     # installa shim e patch nel runner e nel prefix
sudo scripts/setup-vcam.sh            # solo per la webcam; aggiungi --obs se usi OBS Studio (vedi sotto)
```

`setup-prefix.sh` è stato scritto a partire dai passaggi manuali che hanno funzionato e non è mai stato rieseguito su un
prefix nuovo. Se un passaggio fallisce, i commenti nello script spiegano a cosa serve ciascuno.

Si avvia con `nvidia-broadcast` (`deploy.sh` collega il launcher in `~/.local/bin`). Il launcher crea i dispositivi
PipeWire, avvia il feeder della camera se esiste un dispositivo v4l2loopback con label "NVIDIA Broadcast", e avvia
l'app. **Avvia Broadcast prima delle applicazioni che usano i suoi dispositivi.** Si chiude chiudendo la finestra; per
terminare una sessione bloccata usa
`WINEPREFIX=~/Games/umu/nvbroadcast/pfx ~/.local/share/Steam/compatibilitytools.d/GE-Proton11-11-x86_64/files/bin/wineserver -k`.

Dentro Broadcast scegli come microfono/altoparlante/camera i tuoi dispositivi reali. In Discord, OBS, browser e così
via scegli "NVIDIA Broadcast" come microfono, altoparlante o camera. Non scegliere "NVIDIA Broadcast" come camera
dentro Broadcast stesso: gli darebbe in ingresso il suo stesso output.

Variabili d'ambiente: `NVB_RUNNER` e `NVB_PREFIX` cambiano i percorsi di runner e prefix in tutti gli script.

### Voce nel menu delle applicazioni

`deploy.sh` installa anche `~/.local/share/applications/nvidia-broadcast.desktop` a partire dal modello
`nvidia-broadcast.desktop`, così "NVIDIA Broadcast" compare nel menu delle applicazioni e si può fissare. Avvia il
launcher (non il collegamento Wine che Proton crea nel prefix, che salterebbe i dispositivi PipeWire e il feeder della
camera). L'icona è quella che Proton ha estratto dall'app in `<prefix>/pfx/drive_c/proton_shortcuts/icons/`: è
referenziata da lì e non copiata in questo repository. Se non la trova, usa l'icona generica `camera-video`.
`StartupWMClass` permette al desktop di raggruppare la finestra con la voce di menu.

## Coesistenza con OBS Studio

Anche la Virtual Camera di OBS usa v4l2loopback e prende il primo dispositivo loopback libero. Se trovasse il nostro,
ci scriverebbe dentro. Perciò `setup-vcam.sh --obs` crea due dispositivi con **una sola** riga di opzioni del modulo in
`/etc/modprobe.d/99-nvidia-broadcast-vcam.conf`:

```
options v4l2loopback devices=2 video_nr=9,10 card_label="OBS Virtual Camera,NVIDIA Broadcast" exclusive_caps=1,1
```

`/dev/video9` è di OBS e ha il numero più basso, `/dev/video10` è il nostro. Il launcher trova il nostro dalla label.
Note:

- Tutto deve stare su una riga: le opzioni di più righe `options v4l2loopback` non vengono unite, vince l'ultimo valore
  di ogni parametro (Fedora ne installa una per OBS in `/usr/lib/modprobe.d/98-v4l2loopback.conf`, la nostra si chiama
  `99-*` così viene letta per ultima).
- Una lista di label va tra virgolette come insieme (`"A,B"`). Mettere le virgolette attorno a ogni nome lascia
  caratteri di virgolette nei nomi.
- OBS non era installato sulla macchina di prova: la disposizione a due dispositivi e il fatto che un lettore su
  `video9` non disturbi sono stati verificati con un piccolo lettore V4L2 (`src/tools/v4lgrab.c`), non con OBS vero.

## Come funziona

L'app Windows così com'è non è utilizzabile. Ognuno dei punti seguente era un guasto distinto da capire e correggere.

**Installer.** `setup.exe` vuole `eula.txt` accanto a `setup.cfg`, la chiave di registro
`HKLM\Software\Microsoft\Windows\CurrentVersion\SharedDLLs` e la cartella `C:\ProgramData\NVIDIA\NGX\models`.

**CUDA.** GE-Proton con `PROTON_NVIDIA_LIBS=1` fornisce `nvcuda.dll` come wrapper sopra la libcuda dell'host. Mancano
tre cose, quindi `src/nvcuda-shim` sostituisce il wrapper (l'originale resta come `nvcuda_real.dll`):
- `cuGetProcAddress` riconosceva solo le versioni "introdotta in" esatte; la shim restituisce la migliore versione non
  superiore a quella richiesta, come fa il driver vero.
- Il core confronta il nome della GPU riportato da CUDA ("... 4060 Laptop GPU") con quello di DXVK-NVAPI
  ("... 4060M") e rifiuta se differiscono; la shim riporta il nome NVAPI.
- Gli effetti video usano una tabella di export CUDA non documentata (UUID `d2688bf2-8371-e24f-9469-8177c8ccec27`, una
  funzione) che il wrapper Wine non ha. La shim chiama la funzione nativa nella `libcuda.so.1` dell'host tramite un
  thunk di convenzione di chiamata Windows-SysV. La funzione si trova a un **offset fisso dentro libcuda di una
  specifica versione del driver** (`C4G_OFFSET` in `shim.c`); il suo indirizzo si ricava da `/proc/<pid>/maps` di un
  altro processo che ha libcuda caricata, e si verifica confrontando i primi 11 byte.

**NVAPI.** `src/nvapi-shim` aggiunge `NvAPI_GPU_GetSerialNumber` (id `0x14B83A5F`) a DXVK-NVAPI (originale conservato
come `nvapi64_real.dll`).

**Modelli AI.** L'updater Linux `nvidia-ngx-updater` riceve errori HTTP dal server NVIDIA; il `nvngx_update.exe` per
Windows funziona sotto Wine con `PROTON_ENABLE_NGX_UPDATER=1`.

**Audio.** Il driver audio virtuale di Windows (`nvrtxvad.sys`) non può esistere sotto Wine. La sua libreria user-mode
`nvrtxaudcap64v.dll` è sostituita da `src/vad-replacement`, che implementa la piccola tabella di funzioni chiamata dal
core (apri sessione, controlla endpoint predefinito, start/stop, invia un frame da 10 ms a 48 kHz stereo S16) e scrive
i frame su un endpoint di rendering di Wine. Il launcher crea dei loopback PipeWire in modo che questo endpoint diventi
un microfono Linux: `NvB Mic Input` (sink in cui scrive l'app) verso `NVIDIA Broadcast` (sorgente). Per
l'altoparlante, il sink `NVIDIA Broadcast` viene catturato in loopback dal core, elaborato e riprodotto sulla tua
uscita reale.

**Ingresso webcam.** `qcap` di Wine (il filtro DirectShow di cattura V4L2) ha due lacune che il core non tollera.
`scripts/patch-qcap.py` applica queste patch al runner GE:
- `AvgTimePerFrame` non viene mai impostato e il core ne ricava il frame rate. Patch in `qcap.so`.
- I frame non hanno timestamp e il core scarta tutti i frame per cui `IMediaSample::GetTime` fallisce. La patch in
  `qcap.dll` imposta un timestamp relativo al primo frame (con tempi assoluti il renderer si blocca).

Le patch usano spazi liberi riempiti di zeri e controllano prima i byte originali, quindi si applicano solo alla build
esatta per cui sono state scritte. Il core (`NvBroadcastCore.dll`) non viene mai modificato: verifica la propria
firma.

**Uscita webcam.** Il core pubblica i frame elaborati al filtro DirectShow `NvVirtualCameraFilter_x64.dll` tramite
memoria condivisa.
1. `src/vcam-bridge/bridge.c` (programma Windows avviato dentro il prefix dalla shim nvcuda quando
   `NVB_VCAM_BRIDGE=1`) legge da quel filtro e scrive frame BGRA in `/dev/shm/nvb-vcam` (formato in `vcam_shm.h`).
   Collega direttamente camera, sample grabber e null renderer; `RenderStream` inserirebbe uno Smart Tee che va in
   deadlock.
2. `src/vcam-bridge/feeder.c` (Linux) converte in YUYV 1280x720 e scrive a 30 fps sul dispositivo v4l2loopback. Una
   volta al secondo controlla anche se un altro processo ha aperto il dispositivo loopback e lo comunica al bridge
   tramite l'header condiviso. Il bridge esegue il suo grafo, e quindi tiene accesa la webcam vera, solo mentre
   qualcuno guarda quella virtuale. Senza un frame recente da 1,5 s invia nero.

## Elaborazione file (scheda File)

A Wine mancano tre cose di cui il core ha bisogno per elaborare i file. Sono tutte fornite senza toccare
`NvBroadcastCore.dll` (ha il controllo della firma; lo shim reindirizza a runtime gli import del core da
`mfreadwrite.dll`/`mfplat.dll`, `src/nvcuda-shim/mfhooks.c`):

- **Output WAV**: il sink writer di Wine non sa creare un `.wav` (`MFCreateSinkWriterFromURL` fallisce con `0x80004003`,
  errore del job -9111). Lo shim fornisce un piccolo `IMFSinkWriter` WAV.
- **Input video**: Wine elenca il formato decodificato (NV12) prima di quello compresso (H264) come tipo nativo di uno
  stream video, e il core legge il tipo 0 come codec ("Source video codec is not supported"). Lo shim li riordina.
- **Output MP4**: il `libavformat` incluso in GE-Proton non ha muxer, Wine non ha un encoder AAC (`No AAC encoder MFT
  found`) e il suo media sink MP4 è un abbozzo (`BeginWriting` fallisce con `E_NOTIMPL`). `scripts/build-ffmpeg.sh`
  compila un `libavformat` con i muxer e `nvb_media.dll` (`src/media`): una transform AAC e un sink writer mp4, entrambi
  su FFmpeg. Il video lo codifica il core con NVENC. Servono `gcc`, `make`, `curl`.

## Punti fragili

- **Aggiornamenti del driver**: dopo un aggiornamento del driver NVIDIA l'offset C4G in `src/nvcuda-shim/shim.c` può
  cambiare. Sintomo: nessun effetto video, `C4G: no verified libcuda base` nel log della shim (`/tmp/nvcuda_shim.log`).
  Trova il nuovo offset della funzione i cui primi byte sono `55 31 c0 b9 1f 00 00 00 48 89 e5` in `libcuda.so.1` e
  ricompila.
- **Aggiornamenti del runner**: le patch qcap sono specifiche per GE-Proton11-11. Un aggiornamento del runner sostituisce
  anche i file patchati; riesegui `scripts/deploy.sh` (rifiuta le build sconosciute) e le shim vengono reinstallate.
- **Aggiornamenti di NVIDIA Broadcast**: le sostituzioni della libreria audio e il controllo della firma sul core
- **Aggiornamenti del runner e export video**: `deploy.sh` sostituisce il `libavformat.so.62.12.100` di GE con quello
  compilato da `build-ffmpeg.sh`. Un runner con un'altra versione di FFmpeg richiede il sorgente FFmpeg corrispondente (lo
  script lo controlla).
  possono rompersi.
- **Memoria GPU**: l'app e gli effetti usano diverse centinaia di MiB di VRAM; con altri programmi pesanti sulla GPU è
  comparso una volta un `NV_ERR_NO_MEMORY` nel log del kernel.
- Rumore innocuo nei log: `[CRMAIN] Received empty frame from local processor with status: 5 | Dropping frame`.

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
src/nvcuda-shim           shim CUDA (cuGetProcAddress, nome GPU, tabella C4G, avvio bridge, hook di debug)
src/nvapi-shim            shim NVAPI
src/vad-replacement       sostituzione della libreria del dispositivo audio virtuale
src/vcam-bridge           bridge lato Wine, feeder lato Linux, header condiviso
src/tools/v4lgrab.c       lettore V4L2 minimale per provare la camera virtuale
src/probes                piccole sonde Windows usate nel debug
debug/                    file di comandi gdb e client Chrome DevTools per la UI dell'app
```

`NVB_DEV_EXEC=1` abilita nella shim nvcuda un hook per sviluppatori che esegue i comandi scritti in
`/tmp/nvb_exec.cmd` dentro la sessione Wine in corso (un secondo `umu-run` si blocca mentre l'app è aperta). Non
impostarlo nell'uso normale.

## Licenza e componenti di terzi

Il codice di questo repository è rilasciato con licenza MIT (`LICENSE`). Il progetto non è affiliato né approvato da
NVIDIA. Non contiene software NVIDIA: serve il tuo installer di NVIDIA Broadcast (vedi "Reperire i file") e gli shim
reimplementano soltanto le interfacce delle librerie NVIDIA.

- **FFmpeg** (LGPL 2.1 o successiva): non è nel repository. `scripts/build-ffmpeg.sh` scarica i sorgenti da ffmpeg.org e
  li compila sul tuo computer, senza `--enable-gpl` né `--enable-nonfree`. Se ridistribuisci il `libavformat.so.62` o
  `nvb_media.dll` risultanti (che collega FFmpeg staticamente) devi rispettare la LGPL: testo della licenza, sorgente
  corrispondente e opzioni di configure, possibilità di rilinkare. AAC e H.264 possono inoltre richiedere licenze
  sui brevetti nel tuo paese: verifica prima di distribuire binari.
- **Wine / GE-Proton** (LGPL 2.1 o successiva): `patch-qcap.py` e `deploy.sh` modificano una copia locale e non
  ridistribuiscono nulla.
