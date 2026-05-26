# Installazione su Mac — Tutorial completo

Tutorial passo-passo per compilare e installare **Stereo Compressor** su un Mac dove **non hai mai compilato niente prima**. Per ogni step c'è la versione "happy path" + il fix se il comando manca o fallisce.

> Stima tempo: **15-25 minuti** di lavoro effettivo + 5-15 min di build (dipende dal Mac).

---

## Tabella di marcia

| # | Step | Tempo |
|---|---|---|
| 1 | Verifica versione macOS e architettura | 1 min |
| 2 | Installa Xcode Command Line Tools | 5-10 min |
| 3 | Installa Homebrew | 3-5 min |
| 4 | Installa CMake | 1 min |
| 5 | Scarica/posiziona il progetto | 1 min |
| 6 | Compila il plugin | 5-15 min |
| 7 | Rimuovi la quarantena Gatekeeper | 10 sec |
| 8 | Valida l'AU con auval | 30 sec |
| 9 | Carica in Logic Pro | 2 min |

---

## Step 1 — Verifica il tuo Mac

Apri **Terminale** (`⌘+Spazio` → digita "Terminale" → invio) e controlla:

```bash
sw_vers
uname -m
```

Output atteso (esempio):
```
ProductName:    macOS
ProductVersion: 15.2
BuildVersion:   24C101
arm64
```

- `ProductVersion` deve essere **≥ 11.0**. Se sei su macOS 10.x non puoi installare le ultime Xcode CLT → aggiorna macOS prima di proseguire.
- `uname -m`: `arm64` = Apple Silicon (M1/M2/M3/M4), `x86_64` = Intel. Il build è universal, funziona su entrambi.

---

## Step 2 — Xcode Command Line Tools

Servono per il compilatore C++ (`clang`) e gli header di sistema.

### Tentativo:
```bash
xcode-select --install
```

Si apre una finestra di dialogo → clicca **Install** → accetta i termini → aspetta ~5-10 min.

### Fix #1 — *"command not found: xcode-select"*

Vuol dire che macOS è stranamente vecchio o corrotto. Scarica manualmente da:
- https://developer.apple.com/download/all/ → cerca "Command Line Tools for Xcode" → versione compatibile con la tua macOS.

### Fix #2 — *"xcode-select: error: command line tools are already installed"*

Già fatto, vai allo step 3.

### Fix #3 — *"Can't install the software because it is not currently available from the Software Update server"*

Bug noto. Reset e riprova:
```bash
sudo rm -rf /Library/Developer/CommandLineTools
sudo xcode-select --reset
xcode-select --install
```

### Fix #4 — Hai Xcode completo (dall'App Store)?

Anche meglio. Accetta la licenza e seleziona il toolchain:
```bash
sudo xcodebuild -license accept
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
```

### Verifica:
```bash
clang --version
```
Deve stampare `Apple clang version XX.x.x`. Se sì → procedi.

---

## Step 3 — Homebrew

Package manager standard su macOS, ci serve per installare CMake.

### Tentativo:
```bash
brew --version
```

Se stampa una versione (`Homebrew 4.x.x`) → vai allo step 4.

### Fix — *"command not found: brew"*

Installa Homebrew:
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

A fine installazione lo script stampa **due righe** simili a queste (variano per arch):

**Su Apple Silicon (M1/M2/M3/M4):**
```bash
echo >> ~/.zprofile
echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
eval "$(/opt/homebrew/bin/brew shellenv)"
```

**Su Intel:**
```bash
echo 'eval "$(/usr/local/bin/brew shellenv)"' >> ~/.zprofile
eval "$(/usr/local/bin/brew shellenv)"
```

**Copia ed esegui quelle righe** (lo script te le dice, non inventarle). Altrimenti `brew` non sarà nel PATH e dovrai riaprire il terminale.

### Sub-fix — *"curl: command not found"*

Non dovrebbe mai succedere su macOS, ma se accade installa `curl` via Xcode CLT (step 2) prima di Homebrew.

### Sub-fix — Homebrew chiede la password sudo

Normale, è la password del tuo utente macOS. Digita (non vedi caratteri) e invio.

### Verifica:
```bash
brew --version
```

---

## Step 4 — CMake

Il sistema di build che genera il progetto Xcode dal nostro `CMakeLists.txt`.

### Tentativo:
```bash
cmake --version
```

Se stampa `cmake version 3.22` o superiore → vai allo step 5.

### Fix — *"command not found: cmake"* o versione vecchia:
```bash
brew install cmake
```

Aspetta 1-2 min.

### Fix — *"Error: cmake: no bottle available"*

Probabilmente sei su un'arch non standard. Forza la build da sorgente:
```bash
brew install --build-from-source cmake
```
(Richiede ~5-10 min in più ma funziona sempre.)

### Verifica:
```bash
cmake --version
```
Deve essere `≥ 3.22`.

---

## Step 5 — Posiziona il progetto

Se il progetto è già in `~/Idee/StereoCompressor` (o ovunque) → skip.

Altrimenti, esempio se hai uno zip:
```bash
cd ~/Downloads
unzip StereoCompressor.zip -d ~/
cd ~/StereoCompressor
```

### Verifica di essere nella cartella giusta:
```bash
ls
```
Devi vedere: `CMakeLists.txt`, `source/`, `README.md`, `INSTALL.md`.

---

## Step 6 — Compila

### Configura:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

Prima volta: scarica JUCE 8.0.8 da GitHub (~300 MB, 1-3 min a seconda della connessione). Output finale atteso:
```
-- Configuring done
-- Generating done
-- Build files have been written to: .../StereoCompressor/build
```

> **Nota**: questo usa il generatore di default (Unix Makefiles), che richiede solo le Command Line Tools. Se hai installato **Xcode completo** dall'App Store puoi aggiungere `-G Xcode` per ottenere un progetto Xcode (utile se vuoi aprire il codice nell'IDE). **Non aggiungere `-G Xcode` se hai solo le CLT** — CMake fallisce con un errore fuorviante ("Xcode 1.5 not supported").

### Compila:
```bash
cmake --build build --config Release -j
```

`-j` parallellizza usando tutti i core disponibili (build più veloce).

5-15 minuti. Stampa centinaia di righe `[ XX%] Building CXX object ...`. Output finale atteso:
```
** BUILD SUCCEEDED **
```

### Fix — *"CMake Error: Xcode 1.5 not supported"* o *"Xcode could not be found"*
Hai solo le Command Line Tools, non Xcode completo. Soluzione: rimuovi `-G Xcode` dal comando (è quello che fa la doc qui sopra). Se vuoi davvero il generatore Xcode, installa Xcode dall'App Store e poi:
```bash
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -license accept
```

### Fix — *"No CMAKE_CXX_COMPILER could be found"*
Le CLT non sono attive. Forza:
```bash
sudo xcode-select -s /Library/Developer/CommandLineTools
```
Poi riprova `cmake -B build ...`.

### Fix — Build fallisce con *"error: 'std::byte' has not been declared"* o errori in JUCE
Versione di JUCE incompatibile con Xcode molto recente. Cancella e riprova (riscarica versione aggiornata):
```bash
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

### Fix — Build si blocca su *"AudioUnitSDK"* o errori di linking AU
Su Xcode 16 a volte serve forzare il SDK target. Riprova così:
```bash
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_SYSROOT=macosx
cmake --build build --config Release -j
```

### Fix — *"FetchContent failed: could not clone JUCE"*
Connessione/firewall blocca GitHub. Verifica:
```bash
git clone https://github.com/juce-framework/JUCE.git /tmp/test-juce
```
Se fallisce → problema di rete (VPN aziendale? proxy?). Risolvi e riprova.

### Verifica:
```bash
ls ~/Library/Audio/Plug-Ins/Components/ | grep -i stereo
ls ~/Library/Audio/Plug-Ins/VST3/ | grep -i stereo
```
Devi vedere `Stereo Compressor.component` e `Stereo Compressor.vst3`.

### Verifica universal binary:
```bash
file ~/Library/Audio/Plug-Ins/Components/"Stereo Compressor.component"/Contents/MacOS/"Stereo Compressor"
```
Output atteso:
```
Mach-O universal binary with 2 architectures: [x86_64] [arm64]
```

---

## Step 7 — Gatekeeper / quarantena

**Step più importante e meno documentato.** Da macOS 12 in poi i plugin non firmati con Developer ID vengono bloccati. Senza questo step Logic dirà "failed validation" e il plugin non apparirà.

```bash
xattr -cr ~/Library/Audio/Plug-Ins/Components/"Stereo Compressor.component"
xattr -cr ~/Library/Audio/Plug-Ins/VST3/"Stereo Compressor.vst3"
```

`xattr -cr` = clear all extended attributes, recursive. Toglie il flag `com.apple.quarantine` aggiunto da macOS al binario.

### Verifica:
```bash
xattr ~/Library/Audio/Plug-Ins/Components/"Stereo Compressor.component"
```
Non deve stampare nulla. Se stampa `com.apple.quarantine` → rilancia `xattr -cr`.

### Fix — *"xattr: command not found"*
Non succede mai su macOS (è un comando di sistema). Se succede, le CLT sono rotte → rifai step 2.

---

## Step 8 — Validazione AU

Logic farà questo test automaticamente al primo lancio. Farlo prima evita di scoprire problemi *dentro* Logic.

```bash
auval -v aufx Scmp Mypl
```

Spiegazione codice: `aufx` = audio effect, `Scmp` = PLUGIN_CODE, `Mypl` = MANUFACTURER_CODE (entrambi definiti in [CMakeLists.txt](CMakeLists.txt)).

Output atteso (ultime righe):
```
* * PASS
--------------------------------------------------
AU VALIDATION SUCCEEDED.
```

### Fix — *"OpenAComponent: error -3000"*
Quarantena ancora presente → ritorna allo step 7.

### Fix — *"Initialization: FAIL"*
Mismatch di deployment target. Ricontrolla che in [CMakeLists.txt](CMakeLists.txt) ci sia `CMAKE_OSX_DEPLOYMENT_TARGET "11.0"` impostato **prima** di `project()`. Poi:
```bash
rm -rf build
cmake -B build -G Xcode -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
xattr -cr ~/Library/Audio/Plug-Ins/Components/"Stereo Compressor.component"
```

### Fix — *"AudioUnit not found"*
La build ha generato solo VST3, non l'AU. Verifica che in [CMakeLists.txt](CMakeLists.txt) ci sia `FORMATS AU VST3`.

### Fix — auval non c'è (rarissimo)
`auval` è installato con Logic Pro o con Audio Hijack/Soundtoys. Se non hai Logic ancora installato → skippa lo step 8 e vai dritto allo step 9.

---

## Step 9 — Carica in Logic Pro

1. Apri **Logic Pro**.
2. Menu **Logic Pro → Settings → Plug-in Manager** (su Logic ≤ 10.7: Preferences → Plug-in Manager).
3. Nella colonna a sinistra cerca **MyPlugins**.
4. Trovi `Stereo Compressor` con stato `passed`. Se è `failed` → clicca **Reset & Rescan Selection** in alto a destra.
5. Crea una traccia audio → **Audio FX** (insert) → **Audio Units → MyPlugins → Stereo Compressor**.

### Fix — Il plugin non appare proprio nel Plug-in Manager
- Chiudi Logic completamente (`⌘+Q`).
- Riapri Logic. Al primo avvio scansiona AU nuovi.
- Se ancora niente:
  ```bash
  killall -9 AudioComponentRegistrar 2>/dev/null
  rm -rf ~/Library/Caches/AudioUnitCache
  ```
  Poi riapri Logic.

### Fix — Logic mostra "failed validation"
- Step 7 non fatto. Rifai `xattr -cr` e poi **Reset & Rescan Selection** in Logic.

### Fix — Il plugin appare ma il pannello è bianco / non si vede
Più raro, succede con cache GPU. Chiudi Logic, poi:
```bash
rm -rf ~/Library/Caches/com.apple.logic10
```
Riapri.

---

## Disinstallare

```bash
rm -rf ~/Library/Audio/Plug-Ins/Components/"Stereo Compressor.component"
rm -rf ~/Library/Audio/Plug-Ins/VST3/"Stereo Compressor.vst3"
rm -rf ~/Library/Caches/AudioUnitCache
```

Poi riapri Logic e fai **Reset & Rescan**.

---

## Aggiornare dopo aver modificato il codice

```bash
cd ~/Idee/StereoCompressor      # o dove hai il progetto
cmake --build build --config Release -j
xattr -cr ~/Library/Audio/Plug-Ins/Components/"Stereo Compressor.component"
```

Non serve riconfigurare CMake né riscaricare JUCE — solo `--build`. Logic ricarica il plugin automaticamente al prossimo avvio (o, in alcuni casi, devi togliere e rimettere l'insert).

---

## Build da zero pulito (se qualcosa va storto)

```bash
cd ~/Idee/StereoCompressor
rm -rf build
rm -rf ~/Library/Audio/Plug-Ins/Components/"Stereo Compressor.component"
rm -rf ~/Library/Audio/Plug-Ins/VST3/"Stereo Compressor.vst3"
rm -rf ~/Library/Caches/AudioUnitCache
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
xattr -cr ~/Library/Audio/Plug-Ins/Components/"Stereo Compressor.component"
xattr -cr ~/Library/Audio/Plug-Ins/VST3/"Stereo Compressor.vst3"
auval -v aufx Scmp Mypl
```

Se questa sequenza esce con `AU VALIDATION SUCCEEDED` → in Logic funzionerà al 100%.

---

## Riassunto comandi minimi (per chi ha già tutto installato)

```bash
cd ~/Idee/StereoCompressor
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
xattr -cr ~/Library/Audio/Plug-Ins/Components/"Stereo Compressor.component"
xattr -cr ~/Library/Audio/Plug-Ins/VST3/"Stereo Compressor.vst3"
```

Apri Logic → fatto.
