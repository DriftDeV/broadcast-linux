# Installazione

[English](../install.md) | [Torna al README](../../README.it.md)

In questa pagina: [cosa serve](#cosa-serve), [installare i requisiti](#installare-i-requisiti),
[ottenere i file di NVIDIA](#ottenere-i-file-di-nvidia), [installare](#installare), [avviare](#avviare).

## Cosa serve

- Una GPU NVIDIA RTX con driver proprietario. Sull'host devono esserci le librerie CUDA e NGX.
- `umu-launcher` (`umu-run`), PipeWire con `pw-loopback`, `python3` con `venv` e `p7zip` per estrarre il driver Windows.
- GE-Proton11-11 (build x86_64) estratto in `~/.local/share/Steam/compatibilitytools.d/GE-Proton11-11-x86_64`.
  UMU-Proton non funziona perché il suo `nvcuda.dll` è vuoto. Con altre build di GE le patch binarie di
  `scripts/patch-qcap.py` vanno ricavate di nuovo, e lo script rifiuta di applicarle alla cieca.
- `v4l2loopback` per la webcam. Su Fedora: `sudo dnf install akmod-v4l2loopback v4l2loopback` (RPM Fusion). Con
  Secure Boot il modulo dell'akmod deve essere firmato con una chiave MOK registrata.
- Per la scheda File con i video: `gcc`, `make` e `curl`.
- L'installer Windows di NVIDIA Broadcast, estratto in una cartella con `setup.exe` e `setup.cfg`, e `nvngx_update.exe`
  da un pacchetto driver NVIDIA per Windows (`Display.Driver/` dentro il driver estratto; è stata usata la versione
  616.64).

## Installare i requisiti

Solo i comandi per Fedora sono stati provati. I nomi dei pacchetti per Debian, Ubuntu e Arch seguono la nomenclatura
abituale delle distribuzioni e possono differire nella tua versione. Se uno non si trova, cercalo con il gestore
pacchetti.

### Fedora

Attiva prima [RPM Fusion](https://rpmfusion.org/Configuration), per il driver NVIDIA e per v4l2loopback.

```
sudo dnf install python3 pipewire-utils 7zip umu-launcher
sudo dnf install akmod-nvidia xorg-x11-drv-nvidia-cuda akmod-v4l2loopback v4l2loopback
```

### Debian e Ubuntu

```
sudo apt install python3 python3-venv pipewire-bin 7zip dkms linux-headers-$(uname -r) v4l2loopback-dkms
# Driver NVIDIA con CUDA: Ubuntu: sudo ubuntu-drivers install   Debian: sudo apt install nvidia-driver libcuda1
```

`7zip` si chiama `p7zip-full` nelle versioni più vecchie. `umu-launcher` non ha un pacchetto apt, quindi installa i file
`.deb` dalle [release di umu-launcher](https://github.com/Open-Wine-Components/umu-launcher/releases). Con Secure Boot,
DKMS chiede di registrare una chiave MOK la prima volta (`mokutil`). Riavvia e conferma.

### Arch

```
sudo pacman -S python pipewire 7zip dkms linux-headers nvidia-utils v4l2loopback-dkms
yay -S umu-launcher            # AUR, oppure pacman -S umu-launcher se i tuoi repository lo hanno
```

Per il driver vero e proprio usa `nvidia-open-dkms` o `nvidia-dkms` (o `nvidia` / `nvidia-open` per il kernel standard).
Con Secure Boot i moduli DKMS vanno firmati a mano.

### GE-Proton11-11 (tutte le distribuzioni)

Scarica il tarball x86_64 dalle [release di GE-Proton](https://github.com/GloriousEggroll/proton-ge-custom/releases) ed
estrailo:

```
mkdir -p ~/.local/share/Steam/compatibilitytools.d
tar -xf GE-Proton11-11*.tar.* -C ~/.local/share/Steam/compatibilitytools.d
ls ~/.local/share/Steam/compatibilitytools.d     # se il nome cartella differisce da GE-Proton11-11-x86_64, imposta NVB_RUNNER
```

### Controllo lato NVIDIA

Proton ha bisogno delle librerie lato Windows del driver, che i pacchetti del driver proprietario installano. Questo
comando deve elencare `nvngx.dll`:

```
ls /usr/lib*/nvidia/wine/nvngx.dll
```

## Ottenere i file di NVIDIA

Servono due file di NVIDIA. Non sono in questo repository e li scarichi tu, secondo i termini di NVIDIA. Tienili fuori
dal repository, per esempio in `~/nvidia-files`:

```
mkdir -p ~/nvidia-files
```

### L'installer di NVIDIA Broadcast

Il file è `NVIDIA_Broadcast_v2.3.0.<build>.exe`, circa 190 MB. Scaricalo dalla
[pagina di NVIDIA Broadcast](https://www.nvidia.com/en-us/geforce/broadcasting/broadcast-app/) in `~/nvidia-files`. È un
archivio autoestraente che si espande a circa 730 MB. Estrailo con 7-Zip invece di eseguirlo:

```
7z x ~/nvidia-files/NVIDIA_Broadcast_v2.3.0.*.exe -o$HOME/nvidia-files/broadcast-installer
ls ~/nvidia-files/broadcast-installer        # deve contenere setup.exe, setup.cfg, NvMaxineClient, NvMaxineModels ...
```

`broadcast-installer` è la cartella da passare come `--installer`. La versione provata è la 2.3.0.

### nvngx_update.exe

È l'updater dei modelli, circa 1 MB, e viene dal pacchetto driver NVIDIA per Windows. Nella
[pagina di download dei driver NVIDIA](https://www.nvidia.com/Download/index.aspx) scegli Windows 10/11 64-bit e la tua
GPU, e scarica il `.exe` in `~/nvidia-files` (la versione provata è la 616.64). Estrai solo quel file:

```
7z x ~/nvidia-files/616.64-*.exe -o$HOME/nvidia-files/driver Display.Driver/nvngx_update.exe
ls ~/nvidia-files/driver/Display.Driver/nvngx_update.exe
```

Anche l'installer del driver è un archivio 7-Zip, quindi `7z` lo apre senza Windows. Se nella tua versione del driver il
file non è in quel percorso, cercalo nell'elenco con `7z l driver.exe | grep -i ngx`. Il percorso del file estratto è
quello da passare come `--nvngx-update`. Il `nvidia-ngx-updater` per Linux non lo sostituisce, perché il server dei
modelli di NVIDIA ne rifiuta la piattaforma.

A installazione finita puoi cancellare la cartella da 730 MB e i `.exe` scaricati. Li legge solo `setup-prefix.sh`.

## Installare

```
git clone <questo repo> && cd broadcast-linux

scripts/build.sh                      # compila tutto in build/ (installa Zig in ~/.cache/nvb-build, senza root)
scripts/build-ffmpeg.sh               # solo per la scheda File con i video: scarica FFmpeg 8.1 e compila due parti
scripts/setup-prefix.sh --installer ~/nvidia-files/broadcast-installer \
                        --nvngx-update ~/nvidia-files/driver/Display.Driver/nvngx_update.exe
scripts/deploy.sh                     # installa shim e patch nel runner e nel prefix
sudo scripts/setup-vcam.sh            # solo per la webcam; aggiungi --obs se usi OBS Studio (vedi obs.md)
```

`setup-prefix.sh` è stato scritto a partire dai passaggi manuali che hanno funzionato e non è mai stato rieseguito su un
prefix nuovo. Se un passaggio fallisce, i commenti nello script spiegano a cosa serve ciascuno.

Le variabili d'ambiente `NVB_RUNNER` e `NVB_PREFIX` cambiano i percorsi di runner e prefix in tutti gli script.

## Avviare

Esegui `nvidia-broadcast`. `deploy.sh` collega il launcher in `~/.local/bin`. Il launcher crea i dispositivi PipeWire,
avvia il feeder della camera se esiste un dispositivo v4l2loopback con label "NVIDIA Broadcast", e avvia l'app.

Avvia Broadcast prima delle applicazioni che usano i suoi dispositivi. Per chiuderlo usa Esci dall'icona nella tray
(tasto destro). Se una sessione si blocca, terminala con:

```
WINEPREFIX=~/Games/umu/nvbroadcast/pfx ~/.local/share/Steam/compatibilitytools.d/GE-Proton11-11-x86_64/files/bin/wineserver -k
```

Dentro Broadcast scegli come microfono, altoparlante e camera i tuoi dispositivi reali. In Discord, OBS, browser e così
via scegli "NVIDIA Broadcast" come microfono, altoparlante o camera. Non scegliere "NVIDIA Broadcast" come camera dentro
Broadcast stesso, perché gli darebbe in ingresso il suo stesso output.

### Voce nel menu delle applicazioni

`deploy.sh` installa anche `~/.local/share/applications/nvidia-broadcast.desktop` a partire dal modello
`nvidia-broadcast.desktop`, così "NVIDIA Broadcast" compare nel menu delle applicazioni e si può fissare. La voce avvia
il launcher. Non avvia il collegamento Wine che Proton crea nel prefix, che salterebbe i dispositivi PipeWire e il feeder
della camera.

L'icona è quella che Proton ha estratto dall'app in `<prefix>/pfx/drive_c/proton_shortcuts/icons/`. È referenziata da
lì e non copiata in questo repository. Se non la trova, usa l'icona generica `camera-video`. `StartupWMClass` permette al
desktop di raggruppare la finestra con la voce di menu.
