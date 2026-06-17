# Tezuka Dashboard — Documentation de l'interface web

Dashboard de contrôle React pour les cartes SDR Tezuka (Zynq-7020 / AD9363).  
Communication via MQTT WebSocket (`ws://[host]:9001/mqtt`) et WebSocket binaire (`ws://[host]/waterfall` pour le spectre).

---

## Architecture générale

| Fichier | Rôle |
|---|---|
| `Tezuka Dashboard.html` | Point d'entrée ; charge tous les `.jsx` dans l'ordre |
| `Tezuka Dashboard (standalone).html` | Fichier auto-contenu pour distribution (ne pas éditer) |
| `Tezuka Spectrum.html` | Visionneuse de spectre autonome, sans React ni build |
| `app.jsx` | Shell applicatif : `<Sidebar>`, `<Topbar>`, routage hash, panneau Tweaks |
| `data.jsx` | Hook `useLiveData()` — connexion MQTT réelle + simulation 1 Hz ; primitives UI partagées |
| `pages1.jsx` | Dashboard · Spectrum |
| `pages2.jsx` | DATV · Versions · Analysis · Network · Transverter · IQ Tape · Signal Generator · Calibrate · Diagnostic · Reboot · Operator · Kalibrate · Persistent · GPIO · Performance |
| `pages3.jsx` | Architecture (schéma fonctionnel AD9361 cliquable) |
| `charts.jsx` | Primitives graphiques : `Donut`, `BarGauge`, `RadialGauge`, `DialGauge`, `StreamChart`, `Sparkline`, `XYChart` |
| `tuner.jsx` | `<FreqTuner>` — saisie fréquence chiffre par chiffre (molette, clavier, clic) |
| `tweaks-panel.jsx` | `useTweaks` hook + panneau de personnalisation glissable |
| `icons.jsx` | Sprite SVG `<Icon name="..." size={N} />` |
| `styles.css` | CSS complet — thème sombre, propriétés personnalisées accent/densité |
| `vendor/spectrum.js` | Classe `Spectrum` — trace FFT + waterfall canvas, zoom/pan, sweep stitching |
| `vendor/colormap.js` | Palettes de couleurs pour le waterfall |
| `paho-mqtt-min.js` | Client MQTT Paho |

**Routage** : hash-based (`location.hash`). Ajouter une page = nouvelle entrée dans `NAV` + cas dans le `switch` de `page()` + titre dans `TITLES` (tous dans `app.jsx`).

**Données live** : `useLiveData()` abonne à `state/#` et expose un objet `d`. `d.publish(path, value)` envoie `cmd/<path>`.

---

## Navigation (sidebar)

La barre latérale est collapsable (bouton hamburger dans la topbar). Elle affiche :
- **Logo / lien** vers le dépôt GitHub
- Groupes de navigation : *(sans groupe)*, **RF**, **Application**, **System**
- En bas : bouton profil opérateur

| Route | Groupe | Icône |
|---|---|---|
| `dashboard` | — | dashboard |
| `spectrum` | RF | spectrum |
| `arch` | RF | chip |
| `datv` | Application | datv |
| `analysis` | Application (sous-menu de datv) | analysis |
| `transverter` | Application | transverter |
| `iqtape` | Application | tape |
| `siggen` | Application | wave |
| `versions` | System | versions |
| `network` | System | network |
| `diagnostic` | System | pulse |
| `calibrate` | System | target |
| `kalibrate` | System (sous-menu de calibrate) | search |
| `performance` | System | chip |
| `gpio` | System | circuit |
| `persistent` | System | save |
| `reboot` | System | power |
| `operator` | (avatar utilisateur) | user |

**Topbar** : breadcrumb `Tezuka / <Nom page>` · indicateur MQTT (puce verte = connecté) · bouton notification · bouton grille · bouton réglages.

**Tweaks Panel** (icône stylo, activée par le host) :
- Couleur accent primaire (5 choix)
- Couleur accent secondaire (5 choix)
- Densité : `compact` / `regular` / `comfy`
- Labels sidebar : on/off
- Mono readouts : on/off

---

## Page : Dashboard

**Route** `#dashboard`  
Vue d'ensemble des paramètres RF en temps réel avec contrôle direct.

### Carte Baseband (partagée RX + TX)

| Élément | Description |
|---|---|
| **Sample rate** `FreqTuner` | Taux d'échantillonnage commun RX et TX (520 833 S/s – 61,44 MS/s). Change RX + TX en simultané. |
| **Décimation** `MiniSelect` | Filtre FIR RX : None / ×2 / ×4 |
| **Interpolation** `MiniSelect` | Filtre FIR TX : None / ×2 / ×4 |
| **Bandwidth** `FreqTuner` | Bande passante RF (200 kHz – 56 MHz). Checkbox **Auto** : suit automatiquement le sample rate (= SR × 1,5, max 56 MHz). |

### Panneau RX (Receive path)

| Élément | Description |
|---|---|
| **Frequency** `FreqTuner` | Fréquence LO RX (47 MHz – 6 GHz) |
| **Gain control** segmenté | Manual · Slow AGC · Fast AGC — publie `cmd/rx/gain_mode` |
| **RSSI** | Niveau RF reçu en dBm (lecture seule) |
| **RX gain** `Slider` | 0 – 73 dB (grisé en mode AGC) |
| **LED Clipping** | Rouge si `state/rx/overload = 1` |
| **LED Underflow** | Rouge si `state/rx/underflow = 1` |
| **DMA buffer size** | Taille en octets / indicateur de transfert |
| **Input BigStat** | Entrée active RX1/RX2 — clic pour basculer, publie `cmd/rx/rfinput` |

### Panneau TX (Transmit path)

| Élément | Description |
|---|---|
| **Frequency** `FreqTuner` | Fréquence LO TX (47 MHz – 6 GHz) |
| **TX gain** `Slider` | Atténuation −89 à 0 dB (pas 0,25 dB) |
| **LED Clipping** | Rouge si `state/tx/overload = 1` |
| **LED Underflow** | Rouge si `state/tx/underflow = 1` |
| **DMA buffer size** | Taille en octets / indicateur de transfert |
| **Output BigStat** | Sortie active TX1/TX2 — clic pour basculer, publie `cmd/tx/rfinput` |

### Carte Temperature

- `DialGauge` FPGA (°C) — max 65 °C
- `DialGauge` AD9361 (°C) — max 65 °C

### Carte Device

Bandeau d'informations : modèle · uptime · version Tezuka core · état MQTT (connecté/hôte).

---

## Page : Spectrum

**Route** `#spectrum`  
Analyseur de spectre style HP avec affichage canvas phosphore, trace FFT temps réel et waterfall.

### Entête (rangée supérieure)

| Champ | Description |
|---|---|
| **REF** `DbTuner` | Niveau de référence en dBm (signe cliquable pour inverser) |
| **MKR** | Fréquence (MHz) et niveau (dB) sous le curseur — lecture seule |
| **FULL** | Bascule plein écran (`requestFullscreen`) |
| **ANT** | Antenne active RX1/RX2 — clic pour basculer, publie `cmd/rx/rfinput` avec remise à zéro temporaire du gain mode |

### Entête (rangée inférieure)

| Champ | Description |
|---|---|
| **RANGE** `FreqTuner` | Plage dynamique visible en dB (SP_ROWS = 8 div × N dB/div) |
| **dB/DIV** | Calcul automatique (RANGE / 8) — lecture seule |
| **FOSFOR** | Persistance phosphore : OFF → light → medium → high (clic pour cycler) |
| **GAIN** `DbTuner` | Gain RX en dB — publie `cmd/rx/gain` |

### Canvas central

- Connexion WebSocket `ws://[host]/waterfall` (auto-reconnect 2 s)
- Trace FFT phosphore jaune avec gradient de remplissage
- Grille 10×8 divisions
- Curseur : ligne verticale pointillée + point sur trace + étiquette fréquence/niveau

**Interactions souris** :
- `Molette simple` : zoom graphique (×1 à ×8 sans MQTT) ; aux limites, change le span matériel
- `Ctrl+Molette` : zoom span matériel (publie `cmd/rx/span` + `cmd/rx/frequency`)
- `Shift+Molette` : ajuste RANGE (dB par division)
- `Clic-glisser horizontal` : décale la fréquence centrale (publie `cmd/rx/frequency` avec throttle 200 ms)
- `Clic-glisser vertical` : ajuste REF level
- `Survol` : affiche le marqueur de fréquence/niveau

**Interactions tactiles** :
- 1 doigt : pan fréquence (horizontal) + REF (vertical)
- 2 doigts : pinch horizontal = zoom graphique ; pinch vertical = RANGE ; aux limites, change le span matériel

**Clavier** :
- `+` / `=` : REF +5 dB
- `-` / `_` : REF −5 dB
- `[` : RANGE −1 division
- `]` : RANGE +1 division

### Pied (rangée principale)

| Champ | Description |
|---|---|
| **CENTER** `FreqTuner` | Fréquence centrale en kHz (47 000 – 6 000 000 kHz) — publie `cmd/rx/frequency` ou `cmd/rx/sweep/frequency` |
| **VFW** `FreqTuner` | Intervalle waterfall en ms — publie `cmd/spectro/fps` |
| **SPAN** `FreqTuner` | Span en kHz (80 – 300 000 kHz) — publie `cmd/rx/span` avec throttle 500 ms |

### Pied (rangée secondaire)

| Champ | Description |
|---|---|
| **ST** | Période de trame en ms (= 1000 / FPS) — lecture seule |
| **ZOOM** | Facteur de zoom graphique actuel (affiché si > ×1) |

---

## Page : DATV Controller

**Route** `#datv`  
Contrôle du modulateur DVB-S/DVB-S2 PlutoSDR (via MQTT callsign `cmd/pluto/<callsign>/...`).

### Entête

- Titre + indicateur de mode (`callsign · DVB-S2/TS · QPSK`)
- **Témoin ON AIR / STANDBY** avec lampe animée
- **Toggle TX ON/OFF** — publie `cmd/pluto/<call>/tx/mute` (0 = on air)

### Carte Modulator (7/12 colonnes)

| Champ | Description |
|---|---|
| **Frequency** `FreqTuner` | Fréquence TX (47 MHz – 6 GHz) |
| **TX gain** `Slider` | −80 à 0 dB, pas 0,25 |
| **Stream mode** `Select` | Test tone / Passthrough / DVB-S2/TS / DVB-S2/GSE / DVB-S |
| **Symbol rate** `TextInput` | 25 000 – 4 000 000 Bd (visible si DVB-S/DVB-S2) |
| **Constellation** `Select` | QPSK / 8PSK / 16APSK / 32APSK (DVB-S2 uniquement) |
| **FEC** `Select` | Auto / 1/4 / 1/3 / 2/5 / 3/5 / 4/5 / 5/6 / 8/9 / 9/10 ; Auto = mode variable |
| **Pilots** `Select` | Off / On |
| **Frame** `Select` | Long frame / Short frame |
| **FIR rolloff** `Select` | 0,20 standard / 0,15 narrow |

### Carte TS source (5/12 colonnes, DVB-S2 uniquement)

| Champ | Description |
|---|---|
| **Input mode** `Select` | UDP / File / Internal pattern |
| **UDP address:port** `TextInput` | Visible si mode UDP (ex. `239.0.0.1:5004`) |

### Carte Stream status

- **TS bitrate** : valeur MQTT ou estimation (SR × 2 × FEC × 0,88)
- **Buffer queue** : BBframes en attente (rouge si > 100)
- **Current FEC** : FEC courant en mode variable
- **CC error PID** : PID en erreur de continuité (rouge)
- **Firmware** : version du firmware PlutoSDR

---

## Page : Analysis

**Route** `#analysis` (sous-menu de DATV)  
Analyse du flux de transport DVB.

- **Transport stream rate** : `StreamChart` du débit de sortie DVB (Mb/s) — donnée MQTT `d.tsH`
- **Video buffer fill** : `StreamChart` du taux de remplissage du buffer encoder (%) — simulation 1 Hz
- **PID table** : tableau des PIDs actifs (PID / Type / Codec / Bitrate / Continuité)

---

## Page : Spectrum (standalone)

**Fichier** `Tezuka Spectrum.html`  
Visionneuse autonome sans React. Même `Spectrum` canvas qu'intégré, mais sans framework.

Contrôles toolbar :
- **RX input** select → `cmd/rx/rfinput`
- **Gain** slider → `cmd/rx/gain`
- **Floor / Ceiling** sliders → `spectrum.setRange(floor, ceiling)` (affichage uniquement)
- **Molette canvas** → zoom/pan (envoie `cmd/rx/span`, `cmd/rx/frequency`)
- **Clavier** → `spectrum.onKeypress()` (space = pause, c = colormap, m = max-hold…)

Puces de statut WS et MQTT : bleu = connexion, vert = connecté, orange = déconnecté.

---

## Page : Transverter

**Route** `#transverter`  
Mode transverter IIO — boucle ADC → DAC pour conversion de fréquence.

### Carte Transverter mode

- **Loopback** `Toggle` : Off / Active — publie `cmd/rx/loopback` (0 ou 2)

### Carte RX path (grisée si loopback inactif)

| Champ | Description |
|---|---|
| **RX frequency** `FreqTuner` | 47 – 6000 MHz |
| **RX bandwidth** `FreqTuner` | 200 – 56 000 kHz |
| **RX input power** `Slider` | 0 – 73 dB |

### Carte TX path (grisée si loopback inactif)

| Champ | Description |
|---|---|
| **TX frequency** `FreqTuner` | 47 – 6000 MHz |
| **TX bandwidth** `FreqTuner` | 200 – 56 000 kHz |
| **TX output power** `Slider` | −89,75 à 0 dB |

---

## Page : IQ Tape

**Route** `#iqtape`  
Enregistrement et lecture de captures IQ (via WebSocket `ws://[host]:8765`).

### Carte Local folder

| Champ | Description |
|---|---|
| **IQ Tape** toggle | Active/désactive le service `iio_ws_proxy` sur le device — publie `cmd/system/iqtape` |
| **Folder** | Sélection du dossier local (File System Access API en contexte sécurisé, sinon input multi-fichiers) ; restauration depuis IndexedDB au rechargement |

### Carte Capture (enregistrement)

| Champ | Description |
|---|---|
| **Sample rate** `FreqTuner` | Taux d'échantillonnage (modifie RX + TX) |
| **RX frequency** `FreqTuner` | Fréquence RX en cours |
| **TX frequency** `FreqTuner` | Fréquence TX en cours |
| **Format** `Select` | cs8 / cs16 (désactivé pendant l'enregistrement) |
| **Filename** | Généré automatiquement : `YYYYMMDD_HHMMSS_<fréq>_<sr>.<format>` |
| **Bouton Record/Stop** | Lance ou arrête l'enregistrement WebSocket |
| **Indicateur** | Vitesse (MB/s) + volume total enregistré |

### Carte Playback (lecture)

| Champ | Description |
|---|---|
| **File selection** `Select` | Fichiers .iq/.bin/.cf32/.cs16/.cs8/.raw du dossier |
| **Métadonnées** | SR, format, taille, durée (dossiers démo uniquement) |
| **Play / Stop** | Envoie le fichier sur `ws://[host]:8765` (protocole `iio-tx`), streaming 64 KB par trame |
| **Loop** bouton | Boucle la lecture en continu |
| **Progression** | Barre de progression + vitesse (MB/s) |

---

## Page : Signal Generator

**Route** `#siggen`  
Générateur de signaux IQ synthétiques avec envoi WebSocket vers le TX.

### Entête

- Résumé du mode courant (type · SR · amplitude · paramètres sweep)
- **Service on/off** bouton — publie `cmd/system/siggen`
- **Témoin TX ON/OFF** + `Toggle` — démarre/arrête le streaming WebSocket

### Carte RF output

| Champ | Description |
|---|---|
| **TX frequency** `FreqTuner` | Fréquence TX (47 MHz – 6 GHz) |
| **TX gain** `Slider` | −89,75 à 0 dB |
| **Sample rate** `FreqTuner` | Doit correspondre au SR du device |

### Carte Waveform

| Champ | Description |
|---|---|
| **Type** `Select` | CW · Two tone · AM · FM · SSB · Sweep · AWGN |
| **Amplitude** `Slider` | 1 – 100 % FS |
| **Paramètres spécifiques** | Voir tableau ci-dessous |

**Paramètres par type de signal** :

| Type | Paramètres |
|---|---|
| CW | Frequency offset (Hz) |
| Two tone | Center offset (Hz) · Tone spacing (Hz) |
| AM | Carrier offset (Hz) · Mod frequency (Hz) · Mod depth (%) |
| FM | Carrier offset (Hz) · Mod frequency (Hz) · Deviation (Hz) |
| SSB | Audio frequency (Hz) · Sideband (USB/LSB) |
| Sweep | Start freq (Hz) · Stop freq (Hz) · Sweep time (ms) · Shape (Triangle/Sawtooth) |
| AWGN | Amplitude uniquement (3σ = amplitude FS) |

### Carte IQ constellation

Canvas 160 px — nuage de points I/Q sur 2048 échantillons (points bleus).

### Carte Time domain

Canvas 80 px — 512 premiers échantillons : I en bleu (#5bc4ff) · Q en orange (#ff9b4a).

---

## Page : Architecture

**Route** `#arch`  
Schéma fonctionnel interactif du transceiver AD9361.

### Carte AD9361 RF transceiver (SVG cliquable, 8/12 colonnes)

Schéma bloc SVG à l'échelle avec :
- **Contour puce AD9361** + label « AD9361 · 2×2 MIMO »
- **Contour Zynq-7020** (FPGA)
- **Blocs cliquables** (couleur par groupe) :

| Bloc | Groupe | Description |
|---|---|---|
| RX1/RX2 | port (vert) | Ports RF d'entrée |
| LNA + Att | rx (accent) | Ampli faible bruit + atténuateur |
| RX Mixer | rx | Mélangeur quadrature zero-IF |
| BB Filter | rx | Filtre passe-bas baseband RX |
| ADC | rx | Convertisseur Σ-Δ 12 bits |
| Dec / HB | rx | Filtre décimation half-band |
| TX1/TX2 | port (vert) | Ports RF de sortie |
| Driver + Att | tx (rose) | Driver PA + atténuateur numérique |
| TX Mixer | tx | Mélangeur quadrature direct conversion |
| BB Filter | tx | Filtre LPF reconstruction TX |
| DAC | tx | Convertisseur 12 bits |
| Int / HB | tx | Filtre interpolation half-band |
| RX PLL | lo (bleu) | Synthétiseur fractional-N RX |
| TX PLL | lo | Synthétiseur fractional-N TX |
| AuxADC | sys (dim) | ADC auxiliaire + capteur température |
| BB PLL | sys | PLL baseband (référence horloges) |
| SPI / ENSM | sys | Bus SPI 4 fils + machine d'état ENSM |
| Data Port | sys | Interface données I/Q (LVDS/CMOS) |
| Zynq PL | fpga (violet) | FPGA + Tezuka HDL + AXI DMA |

- **Connexions** fléchées colorées par groupe
- **Légende** des groupes

### Panneau de détail (4/12 colonnes)

S'ouvre au clic sur un bloc. Contient :
- Tag de groupe coloré + bouton « Overview »
- Description textuelle du bloc
- Champs éditables (selon le bloc) :
  - `FreqTuner` pour les fréquences LO / sample rates
  - `Slider` pour les gains / bandes passantes
  - `Select` pour les modes (ENSM, interface DDI, entrée/sortie RF…)
  - Lecture seule pour les paramètres figés (résolution ADC, type de PLL, températures live…)

**Barre d'action** (apparaît si changements non appliqués) :
- Bouton **Reset** : annule les modifications locales
- Bouton **Apply** : publie tous les changements modifiés vers le device via MQTT

---

## Page : Versions

**Route** `#versions`

### Carte Platform (5/12 colonnes)

- Icône + modèle + numéro de série
- Tableau clé-valeur : SoC · RF transceiver · GCC target · Build date

### Carte Firmware components (7/12 colonnes)

Tableau des composants : Tezuka / Linux kernel / U-Boot / FPGA bitstream / Root FS / libiio  
Colonnes : Component · Version · Notes · Pill (current/info)

### Carte Update (12/12 colonnes)

- Statut (pill « Up to date »)
- Bouton **Check for updates**
- Bouton **Install from file**

---

## Page : Network

**Route** `#network`

### Carte Network interface (7/12 colonnes)

Onglets **LAN** / **Wi-Fi** / **USB**  
Champs éditables (données live MQTT) :
- IP address · Subnet mask · Gateway · DNS · MAC · Hostname

### Carte MQTT broker (5/12 colonnes, colonne droite)

- Host · Port (1883) · Base topic (`state/#`)
- Pill de statut Connected/Offline + hôte MQTT

### Carte Service ports (5/12 colonnes)

Tableau : HTTP 80 · RTSP 554 · RTMP 1935 · SSH 22 · MQTT 1883

### Carte System & throughput (12/12 colonnes)

`StreamChart` double axe (60 points, 1 Hz) :
- Axe gauche (0–100) : CPU % (accent) · Mem % (bleu) · SoC °C (vert) · FPGA °C (corail)
- Axe droit (0–5 MB/s) : RX B/s (violet) · TX B/s (rose)

---

## Page : Calibrate

**Route** `#calibrate`

### Entête

- Titre + correction TCXO courante en ppm
- Bouton **Kalibrate from RF** (navigue vers `#kalibrate`)

### Carte Frequency calibration (12/12 colonnes)

- Toggle ON/OFF (auto-discipline)
- **Oscillator PPM** `Slider` (−200 à +200 ppm, pas 0,1) + `TextInput` numérique + bouton **Save**
- Publie `cmd/main/freq_correction` (valeur = 40 000 000 + ppm × 40)
- Bouton Save : écrit dans `system/setenv/xo_correction`

### Carte Gain vs frequency (12/12 colonnes)

`XYChart` éditablepar glissement — 56 points de 47 MHz à 6 GHz.  
Bouton **Apply** (apparaît si courbe modifiée) → publie `cmd/main/gain_table_config`.

### Carte DAC gain vs amplitude (12/12 colonnes)

`XYChart` éditable — 40 points de 0 % à 100 % FS.

---

## Page : Kalibrate (from RF)

**Route** `#kalibrate` (sous-menu de Calibrate)  
Calibration de l'oscillateur par scan GSM.

### Entête

- Select de bande : GSM850 / GSM-R / GSM900 / EGSM / DCS
- Bouton **Launch scan** → publie `cmd/system/kalibrate/scan` ; spinner pendant le scan

### Carte XO correction (12/12 colonnes)

- Correction courante en ppm
- Résultat Kalibrate en ppm/ppb (si disponible)
- Bouton **Apply to XO** (si statut = done)

### Carte Status (conditionnelle)

Pill de statut : scanning / calibrating / done / error / writing

### Tableau GSM channels (si canaux disponibles)

Colonnes : Chan · Freq (MHz) · Power (dBFS) · Bouton **Kalibrate**  
Trié par puissance décroissante ; clic = publie `cmd/system/kalibrate/run <chan>`.  
Résultat ppm affiché dans le bouton après calibration.

### Carte kal output (12/12 colonnes)

Fenêtre de log scrollable — stdout brut de `kal`. Bouton **Clear**.

---

## Page : Diagnostic

**Route** `#diagnostic`

### Entête

- Bouton **Show iio debug** → publie `cmd/system/getdebugiio` ; charge `/sys/kernel/debug/iio`
- Bouton **Provide log** → publie `cmd/system/logrequest` ; charge les lignes dmesg

### Carte IIO debug (conditionnelle, 12/12 colonnes)

Fenêtre de log — paires clé/valeur des registres IIO debug (triées alphabétiquement).

### Carte System log (12/12 colonnes)

Fenêtre de log avec horodatage (`HH:MM:SS`) — lignes dmesg.  
Bouton **Clear** (vide l'affichage sans supprimer les données MQTT).

---

## Page : Reboot

**Route** `#reboot`

### Entête

- Uptime courant + mode sélectionné

### Carte Restart device (7/12 colonnes, phase idle)

- **Reboot mode** `Select` : Normal / Safe mode / Bootloader
- Note d'avertissement (interruption TX/streaming)
- Bouton **Reboot now** → demande confirmation → publie `cmd/system/reboot reboot`

### Progression reboot (7/12 colonnes, phases busy/waiting/done)

- **busy** : spinner + compte à rebours (30 s pour reboot, 6 s pour shutdown)
- **waiting** : spinner + « Waiting for MQTT… »
- **done** : icône check + « Back online » + bouton **Done**

### Carte System (5/12 colonnes)

Tableau compact : Model · Firmware · Linux · Uptime · Serial

---

## Page : Operator

**Route** `#operator` (bouton avatar en bas de sidebar)  
Profil station — stocké dans `localStorage` (`tezuka.operator`).

### Carte Operator profile (7/12 colonnes)

- **Operator name** `TextInput`
- **Callsign** `TextInput` (converti en majuscules à la sauvegarde)
- **Grid locator** `TextInput` (ex. `JN18cv`)
- Boutons **Save changes** / **Reset** (actifs si changements non sauvegardés)

### Carte Identity (5/12 colonnes)

Avatar + nom en gras + callsign + locator — prévisualisation en temps réel.

---

## Page : Persistent Storage

**Route** `#persistent`  
Édition des variables d'environnement U-Boot (`fw_printenv` / `fw_setenv`).

### Entête

- Bouton **Refresh** → publie `cmd/system/getenv all`

### Carte New variable (12/12 colonnes)

- `TextInput` **Name** (caractères alphanumériques + underscore uniquement)
- `TextInput` **Value**
- Bouton **Save** → publie `cmd/system/setenv/<name>`

### Carte Environment variables (12/12 colonnes)

- Filtre texte (cherche dans nom ET valeur)
- Tableau trié alphabétiquement : Variable · Value (éditable, multiline si `\n`) · Bouton Save/Reset par ligne
- Pill « saved » 2 s après sauvegarde

---

## Page : GPIO

**Route** `#gpio`  
Contrôle des broches GPIO (données `state/gpio/<n>`).

- **gpio-grid** : tuiles GPIO numérotées
- Chaque tuile : numéro · point coloré (vert = ON / rouge = OFF) · état texte
- Clic sur une tuile → bascule → publie `cmd/gpio/<pin>` (0 ou 1)

---

## Page : Performance

**Route** `#performance`

### Carte CPU (6/12 colonnes)

- **Overclock profile** `Select` : profils disponibles depuis `state/system/overclock_cap` (rechargés à la connexion MQTT)
- Changement → publie `cmd/system/overclock <profile>` (effet au prochain boot)

---

## Conventions MQTT

```
state/<path>   ← publié par le device (retained)
cmd/<path>     → envoyé par le dashboard au device
```

Les valeurs sont des chaînes de caractères décimales. Les drapeaux booléens sont `0`/`1`.  
Topics DVB : `cmd/pluto/<callsign>/<path>` / `dt/pluto/<callsign>/<key>`.

---

## Lancer le dashboard en développement

```bash
cd /path/to/Dashboard
python3 -m http.server 8080
# Ouvrir http://localhost:8080/Tezuka%20Dashboard.html
```

Pour pointer vers un device distant, modifier `MQTT_DEV_HOST` dans `data.jsx` :
```js
const MQTT_DEV_HOST = '10.0.0.61';  // adresse IP du Tezuka
```
