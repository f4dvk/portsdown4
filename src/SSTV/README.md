# RPI_SSTV

Décodeur SSTV (Slow Scan Television) pour Raspberry Pi 3/4 sous Raspberry Pi
OS (Raspbian) **Lite** (sans bureau graphique), avec réception directe
possible depuis une clé RTL-SDR, un PlutoSDR ou un LimeSDR.

Le projet combine trois briques externes :

- le cœur de décodage C++ de
  [dawsonjon/PicoSSTV](https://github.com/dawsonjon/PicoSSTV) (conçu à
  l'origine pour le Raspberry Pi Pico), adapté pour tourner nativement sous
  Linux (`third_party/sstv_library`, MIT) ;
- la technique d'affichage écran de
  [Langstone-V2](https://github.com/g4eml/Langstone-V2) de Colin Durbridge
  G4EML : accès direct au framebuffer Linux (`mmap` de `/dev/fb0`), dessin
  pixel par pixel sans serveur X, et lecture directe d'un écran tactile sur
  `/dev/input/eventX` (`third_party/langstone_gui`, GPLv3) ;
- GNU Radio 3.7 (`gr-osmosdr` pour RTL-SDR, `gr-iio` pour PlutoSDR, `gr-limesdr`
  pour LimeSDR) pour piloter le matériel SDR (accord, démodulation USB/LSB/FM,
  spectre) — voir `sdr/`. C'est la même génération GNU Radio que celle déjà
  utilisée par Langstone-V2 sur Raspberry Pi (Portsdown4) ; les appels aux
  blocs Pluto/Lime sont repris de `Lang_TRX_Pluto.py`/`Lang_TRX_Lime.py`, pas
  seulement de la documentation GNU Radio. Sur Raspberry Pi OS Buster, cette
  version de GNU Radio n'a des bindings que pour **Python 2** (comme
  Langstone-V2 lui-même) : les scripts `sdr/*.py` sont donc écrits en Python 2
  et lancés via `python2` (`sstv_radio --python` permet de changer
  d'interprète sur un système plus récent).

## Trois façons d'utiliser RPI_SSTV

| | `bin/sstv_decode` | `bin/sstv_gui` | `bin/sstv_radio` |
|---|---|---|---|
| Entrée audio | fichier `.wav` ou flux stdin | fichier `.wav` ou flux stdin | RTL-SDR / PlutoSDR / LimeSDR |
| Affichage | aucun (BMP uniquement) | framebuffer, ligne par ligne | framebuffer + spectre + accord |
| Écran tactile | non | bouton "Quitter" (optionnel) | accord, mode, quitter (optionnel) |
| Dépendances | aucune | aucune | GNU Radio 3.7, gr-osmosdr/gr-iio/gr-limesdr (`install_sdr.sh`) |
| Licence | MIT | GPLv3 | GPLv3 |

`sstv_radio` est l'outil à utiliser pour recevoir du SSTV directement depuis
une clé SDR. `sstv_gui`/`sstv_decode` restent utiles pour rejouer un
enregistrement ou capturer depuis une carte son classique (portsdown4, ou
tout récepteur avec une sortie audio).

## Architecture

```
RPI_SSTV/
├── third_party/
│   ├── sstv_library/            Cœur de décodage SSTV (vendored, MIT)
│   │                             — dawsonjon/PicoSSTV
│   └── langstone_gui/           Framebuffer + tactile (vendored, GPLv3)
│                                 — g4eml/Langstone-V2 (voir ORIGIN.md)
├── sdr/
│   ├── common_rx.py              Chaine GNU Radio commune (accord, demod USB/LSB/FM, spectre)
│   ├── rx_rtlsdr.py               Point d'entree RTL-SDR
│   ├── rx_pluto.py                Point d'entree PlutoSDR
│   ├── rx_lime.py                 Point d'entree LimeSDR
│   └── rx_test.py                 Generateur de signal de test (sans materiel)
├── src/
│   ├── sstv_decode.cpp            CLI sans écran (modes "fichier" et "flux")
│   ├── sstv_gui.cpp               CLI avec affichage framebuffer + tactile
│   ├── sstv_radio.cpp             CLI SDR : accord, mode, spectre, image SSTV
│   ├── wav_reader.{h,cpp}         Lecture d'un vrai fichier WAV (entête RIFF)
│   ├── sstv_sources.h             Sources audio partagées (fichier / stdin)
│   ├── bmp_writer_stdio.h         Écriture BMP partagée
│   └── sstv_util.h                Horodatage des fichiers de sortie
├── scripts/
│   ├── install.sh                 Installation de base + compilation
│   ├── install_sdr.sh             Installation GNU Radio + gr-osmosdr/gr-iio/gr-limesdr (optionnel)
│   ├── build.sh                   Recompilation seule
│   ├── decode_file.sh / decode_live.sh   Décodage + affichage via fbi
│   ├── gui_file.sh / gui_live.sh         Décodage + affichage framebuffer
│   ├── radio_rtlsdr.sh / radio_pluto.sh / radio_lime.sh   Réception SDR directe
│   ├── radio_menu.sh              Réception SDR avec menu tactile de sélection
│   ├── radio_test.sh              Réception SDR simulée (sans matériel)
│   └── view.sh                    Affichage d'une image via fbi
├── test_wav/                      Quelques fichiers de test (issus de PicoSSTV)
└── images/                        Dossier de sortie par défaut des images décodées
```

## Installation (sur le Raspberry Pi)

```bash
git clone <url_du_depot> RPI_SSTV
cd RPI_SSTV
./scripts/install.sh
```

Installe `build-essential`, `sox`, `fbi`, et compile `bin/sstv_decode` /
`bin/sstv_gui` / `bin/sstv_radio`.

Pour la réception SDR directe (RTL-SDR/PlutoSDR/LimeSDR), installer en plus
GNU Radio et ses blocs matériels :

```bash
./scripts/install_sdr.sh
```

Ce script installe GNU Radio, `gr-osmosdr` (RTL-SDR), `gr-iio` (PlutoSDR) et
`gr-limesdr` (LimeSDR) séparément, pour qu'un paquet absent n'empêche pas les
autres. Si votre système est déjà une installation Portsdown4/Langstone-V2
existante, `gnuradio`, `gr-osmosdr` et `gr-iio` sont probablement déjà
présents ; seul `gr-limesdr` (nécessaire uniquement pour un LimeSDR) manque
généralement.

`bin/sstv_gui` et `bin/sstv_radio` accèdent directement à `/dev/fb0` (et à
`/dev/input/eventX` si un écran tactile est utilisé) : sur une installation
Raspberry Pi OS standard, l'utilisateur `pi` a déjà les droits nécessaires
(groupes `video` et `input`). Si ce n'est pas le cas sur votre système,
ajoutez votre utilisateur à ces groupes ou lancez les scripts avec `sudo`.

## Réception directe depuis une clé SDR

```bash
./scripts/radio_rtlsdr.sh 14230000 USB     # RTL-SDR, 14.230 MHz, bande laterale superieure
./scripts/radio_pluto.sh  145800000 FM     # PlutoSDR, SSTV ISS, 145.800 MHz FM
./scripts/radio_lime.sh   7171000   LSB    # LimeSDR, 7.171 MHz, bande laterale inferieure

# sans materiel, pour tester l'interface :
./scripts/radio_test.sh

# menu tactile de selection du SDR (necessite --touch) :
./scripts/radio_menu.sh --touch /dev/input/event0
```

L'écran affiche, du haut vers le bas :

- l'en-tête : SDR utilisé, fréquence (groupée par milliers, ex: `144.500.000`), mode ;
- une seule ligne de boutons (accord `-1k`/`-100`/`+100`/`+1k` Hz, mode
  `USB`/`LSB`/`FM`, "Quitter"), pour laisser le plus de place possible à
  l'image SSTV en dessous ;
- un spectre local : une trace réactive (dernier relevé FFT, sans lissage)
  au-dessus d'une cascade/waterfall qui défile avec l'historique, dégradé
  noir→bleu→vert→jaune→rouge — reprise fidèle de la fonction `waterfall()`
  de `LangstoneGUI_RtlSdr.c` (repère orange sur la fréquence accordée) ;
- la zone principale, où l'image SSTV se dessine ligne par ligne au fur et à
  mesure de la réception (le "balayage" visible pendant le décodage), avec
  le mode SSTV détecté (PD120, Robot36, ...) affiché en haut à droite de
  l'en-tête dès la synchronisation.

L'écran tactile est **détecté automatiquement** (comme `detectHw()` dans
`LangstoneGUI_RtlSdr.c` : recherche d'un contrôleur connu — écran officiel
Raspberry Pi, Hyperpixel4/Goodix, DFRobot — dans `/proc/bus/input/devices`),
pas besoin de préciser `--touch` sur un Raspberry Pi avec un de ces écrans.
Un message au démarrage indique le périphérique trouvé (ou son absence) :
```
ecran tactile : /dev/input/event4 (ouvert)
```
`--touch /dev/input/eventX` reste disponible pour forcer un périphérique si
la détection automatique ne le reconnaît pas (contrôleur non listé). Sans
tactile détecté ni forcé, la fréquence et le mode se règlent en ligne de
commande et `Ctrl+C` arrête le programme.

**Mode kiosque (`--kiosk`)** : masque les boutons d'accord et de mode (utile
pour une station laissée en réception fixe sur une fréquence/mode donnés,
pour éviter qu'un appui accidentel sur l'écran tactile ne les change) ; seul
un petit bouton "Exit" reste affiché, en haut à droite du bandeau d'info.
```bash
./scripts/radio_pluto.sh 145800000 FM --kiosk
```

**Commande web (`--web`) -- intégration Portsdown4** : `sstv_radio` peut
recevoir les clics envoyés par la page web de contrôle à distance de
[Portsdown4](https://github.com/BritishAmateurTelevisionClub/portsdown4)
(`webroot/touchscreen/index.html`, qui affiche une capture périodique de
l'écran dans un `<canvas>` et poste les coordonnées de clic en FastCGI sur
le port 2005) : un clic web est alors traité exactement comme un appui
tactile physique, via la même bibliothèque `ffunc.c`/`ffunc.h` que les
autres applications framebuffer de Portsdown4
(`third_party/portsdown_web/`, voir `ORIGIN.md` dans ce dossier).

Désactivée par défaut, la commande web s'active automatiquement si
`sstv_radio` tourne sur une installation Portsdown4 avec
`webcontrol=enabled` dans `portsdown_config.txt`, ou en forçant l'option :

```bash
./bin/sstv_radio --sdr test --wav test_wav/test_pd120.wav --web
```

Nécessite `libfcgi-dev` pour compiler (`-lfcgi -ldl -lpthread -rdynamic`,
déjà dans `scripts/build.sh`). La capture d'écran (`screen.png`) servie par
la page web n'est pas faite par `sstv_radio` lui-même : sur une
installation Portsdown4, elle est prise en charge en externe par
`scripts/screen_grab_for_web.sh` (lancé autour de l'application, comme pour
toutes les applications Portsdown4).

**Début des images** : le décodeur ne commence à écrire une image qu'après
verrouillage sur deux synchros consécutives. `sstv_radio` garde les 2,5
dernières secondes d'audio et, dès la première ligne décodée, relance le
décodage 2 secondes en arrière. Ceci ne récupère que le retard de verrouillage
lié à la structure du signal ; **il ne compense pas un verrouillage tardif dû
au bruit**, car le décodage rejoué est aussi gêné par le bruit. Mesuré sur
signal synthétique : un bruit dans la bande utile égal au signal (0 dB) empêche
tout verrouillage du décodeur amont. La sensibilité réelle dépend donc surtout
du rapport signal/bruit à l'entrée (antenne, gain, filtrage) et d'une meilleure
estimation de la synchro dans `sstv_decoder.cpp`, pas de ce rejeu.

**Images conservées** : seules les 10 dernières images (`--max-images`, par
défaut 10) sont gardées dans le dossier de sortie — les plus anciennes sont
supprimées automatiquement à chaque nouvelle image enregistrée, pour éviter
de remplir la carte SD lors d'une réception laissée longtemps en continu.

**Écoute en direct** : l'audio démodulé est aussi envoyé vers la sortie son
du Raspberry Pi (haut-parleur/casque), en plus du flux envoyé au décodeur.
Le son se coupe automatiquement dès qu'une synchronisation SSTV est
confirmée (pas besoin d'entendre les tonalités SSTV une fois l'image en
cours de décodage), et revient entre deux images.

Fonctionnement interne : `sstv_radio` lance en sous-processus le script
Python correspondant au SDR choisi (`sdr/rx_*.py`, GNU Radio), qui démodule
et renvoie l'audio (PCM 15 kHz) sur sa sortie standard — décodé exactement
comme avec `sstv_gui` — et envoie le spectre local par UDP. L'interface
graphique envoie de simples commandes texte au sous-processus par UDP :
`FREQ <hz>` et `MODE <usb|lsb|fm>` depuis les boutons d'accord/mode, et
`MUTE 0|1` pour l'écoute en direct ci-dessus.

### Si la sensibilité semble faible (RTL-SDR)

Deux réglages à tester, passés après la fréquence/le mode :

```bash
./scripts/radio_rtlsdr.sh 144500000 FM --gain 49          # gain manuel fixe au lieu de l'AGC du R820T
./scripts/radio_rtlsdr.sh 144500000 FM --dc-offset off    # desactive la correction automatique du pic DC
```

L'AGC matérielle des clés RTL-SDR (R820T/R820T2) a une réputation moyenne
(pompage, niveau instable) : un gain manuel fixe (20 à 49 dB selon la clé et
la force du signal) donne souvent de meilleurs résultats pour le décodage
SSTV. La correction DC automatique (`--dc-offset automatic`, activée par
défaut) supprime le pic de fuite d'oscillateur local au centre du spectre,
mais peut dans certains cas affecter le niveau du signal selon le pilote ;
`off` la désactive pour comparer.

En mode FM, la démodulation utilise `analog.nbfm_rx` (comme
`Lang_RX_Rtlhf.py` dans `portsdown4/src/langstone`) avec `max_dev=5 kHz` :
le filtrage est resserré sur la déviation réelle du signal au lieu de
démoduler toute la largeur du premier filtre (~14,5 kHz), ce qui réduit le
bruit hors-canal et améliore la sensibilité sur un signal FM étroit comme le
SSTV ISS.

En USB/LSB, le filtre passe-bande est resserré à 500–2400 Hz (au lieu de
300–2700 Hz, une largeur vocale SSB classique mais inutilement large pour du
SSTV) : le décodeur (`third_party/sstv_library/sstv_decoder.cpp`) n'utilise
que 600–1000 Hz (référence de bruit), 1000–1400 Hz (synchro) et jusqu'à
2300 Hz (pixels), donc couper le reste réduit le bruit sans toucher à ces
bandes. La détection de synchro du décodeur SSTV (seuil adaptatif au bruit
ambiant) n'a pas été modifiée : c'est du code amont éprouvé, et je n'ai pas
de signal réel dégradé ici pour valider un réglage différent sans risque de
régression. Seuls ont été ajoutés des accesseurs en lecture (diagnostic de
synchro) et le suivi de la dernière ligne confirmée par un top de synchro
(`get_valid_rows()`), voir ci-dessous.

Les fréquences d'exemple ci-dessus sont des usages réels : 14.230 MHz USB
(20 m), 7.171 MHz LSB (40 m), 145.800 MHz FM (SSTV depuis l'ISS).

## Décoder un fichier audio ou une carte son (sans SDR)

### Affichage framebuffer (recommandé sur Raspberry Pi)

```bash
./scripts/gui_file.sh mon_enregistrement.wav
./scripts/gui_live.sh default
# avec un écran tactile :
./scripts/gui_live.sh default images 30 --touch /dev/input/event0
```

L'image se dessine ligne par ligne pendant le décodage, une copie BMP est
également enregistrée dans `images/` (nom horodaté). En mode `gui_live.sh`,
l'image reste affichée jusqu'à ce qu'un nouveau signal soit synchronisé.
`Ctrl+C`, ou le bouton tactile "Quitter" si `--touch` est utilisé, arrêtent
proprement le programme.

Le décodeur a besoin de 3 tops de synchro pour se verrouiller : il démarre
donc quelques lignes après le début de l'émission, et la fin de l'image tombe
après la fin de l'émission. Les lignes décodées après le dernier top valide
(fin d'émission, signal perdu) ne sont que du bruit : elles sont retirées de
l'image enregistrée et effacées de l'écran, l'image BMP prenant la hauteur
réellement reçue (ex : 253 lignes au lieu de 256 en Scottie S1).

Fichiers de test fournis :

```bash
./scripts/gui_file.sh test_wav/test_pd120.wav
./scripts/gui_file.sh test_wav/test_robot36.wav
```

### Affichage via fbi (plus simple, sans dessin progressif)

```bash
./scripts/decode_file.sh mon_enregistrement.wav
./scripts/decode_live.sh default
```

Reprend exactement la méthode de Portsdown4 (`sudo fbi -T 1 -noverbose -a`) :
l'image n'est affichée qu'une fois complète.

### Décoder un fichier audio existant, sans écran

```bash
./bin/sstv_decode fichier mon_enregistrement.wav images
```

Fonctionne avec un `.wav` mono ou stéréo, à n'importe quelle fréquence
d'échantillonnage standard (8/16/24/32 bits PCM) : la fréquence réelle du
fichier est lue dans son entête, pas besoin de la préciser.

### Intégration Portsdown4

Le projet reste un dossier autonome (comme `406/`) : il peut être ajouté au
menu `scripts/menu.sh` de Portsdown4 le jour où l'intégration sera
souhaitée, sans rien changer à son fonctionnement autonome sur Raspberry Pi.

## Modes SSTV supportés

Martin M1/M2, Scottie S1/S2/DX, PD50/90/120/180, SC2-60/120/180,
Robot24/36/72, BW8/12 (hérités de PicoSSTV, détection automatique — le
"mode" réglable dans `sstv_radio` est le mode de démodulation radio
USB/LSB/FM, pas le mode SSTV).

## Tester sans matériel SDR

`sdr/rx_test.py` simule un backend SDR (commandes FREQ/MODE, spectre,
audio) en rejouant en boucle un fichier `.wav` de `test_wav/`, sans avoir
besoin de GNU Radio ni de clé SDR :

```bash
./scripts/radio_test.sh test_wav/test_pd120.wav
```

Cela permet de vérifier l'interface (accord, spectre, décodage) avant de
brancher du matériel réel.

**État de la validation sur matériel réel** : `sdr/common_rx.py` cible GNU
Radio 3.7 avec `gr-osmosdr` (RTL-SDR), `gr-iio` (PlutoSDR) et `gr-limesdr`
(LimeSDR) — la génération installée sur les Raspberry Pi qui font déjà
tourner Portsdown4/Langstone-V2. La chaîne **RTL-SDR a été confirmée
fonctionnelle sur un vrai Raspberry Pi** (réception FM 144.5 MHz, correction
DC, AGC, décodage SSTV complet). PlutoSDR et LimeSDR n'ont pas encore été
testés avec le matériel correspondant ; leurs appels de blocs
(`iio.pluto_source`, `limesdr.source`) sont repris tels quels des flowgraphs
réels de Langstone-V2 (`Lang_TRX_Pluto.py`/`Lang_TRX_Lime.py`), qui tournent
depuis longtemps sur ce type de matériel, mais une validation reste à faire.
L'écoute en direct (`audio.sink`) et la coupure automatique du son pendant
le décodage (`MUTE`) sont également nouvelles et pas encore validées sur le
Pi. Cet environnement de développement n'a que GNU Radio 3.10 (API
différente) et aucune clé SDR : `sstv_radio` (interface graphique, gestion
du sous-processus, protocole de contrôle, affichage du spectre) est testé de
bout en bout ici avec `rx_test.py`, qui ne dépend pas de GNU Radio.

Si votre installation utilise GNU Radio 3.9/3.10 (Raspberry Pi OS Bookworm
récent, avec `gnuradio.soapy`), dites-le : la chaîne de démodulation devra
être adaptée en conséquence (les deux versions ont des API Python
incompatibles).

## Licence

`bin/sstv_decode` (et la bibliothèque `third_party/sstv_library`) est sous
licence MIT — voir `third_party/sstv_library/LICENSE.upstream` pour la
licence d'origine de [dawsonjon/PicoSSTV](https://github.com/dawsonjon/PicoSSTV)
(Copyright (c) 2025 dawsonjon).

`bin/sstv_gui` et `bin/sstv_radio` intègrent du code de
[g4eml/Langstone-V2](https://github.com/g4eml/Langstone-V2)
(`third_party/langstone_gui`, GPLv3 — voir `LICENSE.upstream` et
`ORIGIN.md` dans ce dossier pour le détail des modifications apportées). La
licence GPLv3 étant à copyleft fort, l'ensemble du dépôt est distribué sous
GPLv3 (fichier `LICENSE` à la racine), qui reste compatible avec
l'intégration du code MIT de PicoSSTV.

Quelques fichiers `.wav` de test dans `test_wav/` sont issus de PicoSSTV.

## Intégration dans Portsdown4

Cette copie de RPI_SSTV, dans `portsdown4/src/SSTV`, est câblée dans le
système de menus de Portsdown4 exactement comme Langstone V2 :

- **Commande web** : voir la section "Commande web (`--web`)" ci-dessus --
  `sstv_radio` répond aux clics de la page `webroot/touchscreen/index.html`
  via `third_party/portsdown_web/ffunc.c` (copié tel quel depuis
  `src/gui/ffunc.c`, voir `third_party/portsdown_web/ORIGIN.md`).
- **Retour au menu Portsdown à l'Exit** : comme pour Langstone, `sstv_radio`
  ne fait que quitter proprement (`exit(0)`) quand "Exit"/"Quitter" est
  pressé -- ce n'est **pas** lui qui relance le menu principal. C'est
  `scripts/scheduler.sh` (code retour **151**, "Run the SSTV Receiver") qui
  s'en charge : il lance `screen_grab_for_web.sh` avant l'application,
  attend sa sortie (quel que soit son code), puis force
  `GUI_RETURN_CODE=129` pour relancer `rpidatvgui`. C'est le même mécanisme
  que pour tous les autres programmes framebuffer de Portsdown4 (voir les
  cas 145/146/155/156 pour Langstone dans `scheduler.sh`), pas un cas
  spécial pour SSTV.
- **Démarrage** : soit depuis le menu tactile principal (voir ci-dessous),
  soit depuis `scripts/menu.sh` (menu texte whiptail, "Autostart Setup") qui
  propose une option `SSTV_boot` configurant `startup=SSTV_boot` dans
  `portsdown_config.txt` pour démarrer directement sur SSTV. Dans les deux
  cas, c'est le cas `151)` de `scheduler.sh` qui lance la réception (RTL-SDR
  uniquement pour l'instant, mode `--kiosk --web`).
- **Compilation et installation** : `install_portsdown.sh` et `update.sh`
  compilent ce dossier (`cd src/SSTV && ./scripts/build.sh`, juste après
  "Touchscreen Listener"/`rydemon`), puis copient les trois binaires dans
  `/home/pi/rpidatv/bin/` (`cp bin/sstv_decode bin/sstv_gui bin/sstv_radio
  ../../bin/`) -- même convention que `leandvb`/`rydemon`/etc. C'est
  `/home/pi/rpidatv/bin/sstv_radio` que `scheduler.sh` lance (pas la copie
  restée dans `src/SSTV/bin/`), avec `--sdr-dir` et `--out` fixés en absolu
  vers `src/SSTV/sdr` et `src/SSTV/images` : le sous-processus Python
  (`sdr/rx_rtlsdr.py`) et les images décodées restent dans `src/SSTV`, seul
  le binaire est dupliqué dans le `bin/` commun.
- **Menu tactile principal** (`src/gui/rpidatvtouch4.c`) : Menu 5 (Lean DVB)
  → bouton "SSTV" (ex-"RX => TX", en haut à gauche) → Menu 58, qui propose :
  - **RECEIVE** : vérifie qu'une clé RTL-SDR est branchée (`CheckRTL()`,
    même vérification que pour Langstone RtlSdr/RtlHF) puis quitte le menu
    avec le code 151 (`cleanexit(151)`) -- scheduler.sh prend le relais.
  - **Freq** : ouvre le clavier de fréquence partagé (Menu 52, le même que
    pour Lean DVB) et stocke la valeur dans `sstv_frequency`
    (`rx_presets.txt`), **sans toucher** à `rx0frequency` (Lean DVB). Tant
    que `sstv_frequency` n'a pas été définie, le bouton affiche et la
    réception utilise `rx0frequency` -- c'est le sens de "fréquence commune
    avec Lean DVB, mais modifiable depuis le menu SSTV". `scheduler.sh`
    (cas 151) lit `sstv_frequency` puis se rabat sur `rx0frequency` avec la
    même logique, et convertit la valeur (stockée en MHz, comme pour Lean
    DVB) en Hz pour `sstv_radio --freq`.
  - **DATV** / **EXIT** : retour à Menu 5 / Menu 1 (déjà fonctionnel avant
    cette intégration).
  - **SDR^RTL-SDR** et **Config** : affichés mais pas encore interactifs
    (RTL-SDR est pour l'instant la seule source supportée par le bouton
    RECEIVE).

**Limites connues** : pas de sélection PlutoSDR/LimeSDR depuis le menu
tactile (seul RTL-SDR est câblé sur le bouton RECEIVE) ; le bouton "Config"
de Menu 58 ne fait rien encore.

**Non testé sur Raspberry Pi réel** : la commande web, le câblage
`scheduler.sh`/`menu.sh`, et les modifications de `rpidatvtouch4.c` (Menu 58,
partage de fréquence avec Lean DVB via Menu 52) ont été vérifiés par analyse
du code existant (Langstone, `rydemon`, `bandview`, Menu 5 Lean DVB, ...) et,
pour la commande web, par un test de bout en bout en local (compilation avec
`libfcgi-dev`, clic web simulé via `cgi-fcgi` sur le port 2005, vérifié qu'il
déclenche bien la sortie du programme). `rpidatvtouch4.c` n'a en revanche pas
pu être recompilé dans cet environnement de développement (toolchain
spécifique au Raspberry Pi : `bcm_host`, `wiringPi`, `LimeSuite`, absents
hors Pi) -- seule une vérification manuelle du code (cohérence des appels
existants, équilibrage des accolades sur tout le fichier) a été faite. À
valider par une compilation et un test réel sur le Raspberry Pi avant mise
en service.
