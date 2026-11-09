# Décodeur multiple (multidec)

Réception et décodage de 15 modes numériques avec une clé RTL-SDR, dans le
style de l'interface Portsdown 4 (touches en relief de `src/gui/button3d.h`),
et carte web hors ligne des positions décodées.

Accès : **Menu 5 (réception) → Multi Decoder**. L'application a ses propres
menus ; **DATV** ramène au Menu 5, **EXIT** au menu principal.

## Décodeurs

| Touche | Outil | Fréquence par défaut | Vue |
|---|---|---|---|
| POCSAG | multimon-ng 1.6.2 | 439,9875 MHz (DAPNET) | messages |
| FLEX | multimon-ng | 169,650 MHz | messages |
| APRS | direwolf 1.8.1 (1200 / 9600 bauds) | 144,800 MHz | messages, stations + radar |
| RTTY | minimodem 0.24 | 434,250 MHz | texte continu |
| Radiosonde | décodeurs rs1729 (RS41, M10/M20, DFM, RS92, LMS6, iMet, Meisei…) | 403,000 MHz, balayage 400-406 MHz | suivi de sonde |
| ADS-B | readsb 3.16 | 1090 MHz | tableau + radar, liste des avions (apparition, indicatif) |
| ISM 433/868 | rtl_433 25.12 | 433,92 / 868,30 / 868,95 (wM-Bus) / 315 MHz | capteurs |
| AIS | AIS-catcher 0.70 | 161,975 + 162,025 MHz | navires + radar |
| ACARS | acarsdec 4.7 | 131,525 / 131,725 / 131,825 MHz | messages |
| VDL2 | dumpvdl2 2.7 (libacars 2.2) | 136,725 à 136,975 MHz | messages, avions + radar |
| Selcall DTMF | multimon-ng (DTMF + ZVEI1, 2, 3, CCIR ou EEA) | 145,500 MHz | messages |
| CW | multimon-ng (tonalité 500 à 800 Hz) | 144,050 MHz | texte continu |
| Digital Voice | dsd-fme (auto, DMR, dPMR, NXDN48/96, D-STAR, YSF C4FM, M17, P25 ; voix sur la sortie audio) | 439,400 MHz | messages |
| FT8 | ft8_lib 2.0 (FT8 / FT4) | 14,074 MHz | messages, stations + carte |
| WSPR | rtlsdr-wsprd 0.5.7 (envoi à WSPRnet désactivé par défaut) | 14,0956 MHz | messages, stations + carte |
| FM RDS | redsea 1.3.1 (écoute FM en option) | 106,7 MHz, balayage 87,5-108 MHz | station, radiotexte, heure + stations |
| DAB+ | welle-cli (welle.io 2.7) | 201,072 MHz (8D), balayage de la bande III | multiplex, heure + services |

Pour le HF (RTTY, CW, FT8, WSPR) : RTL-SDR Blog V4 (reconnue par la librtlsdr
de Portsdown), ou échantillonnage direct (Config → HF input → Direct Q).
FT8 et WSPR exigent une heure juste (NTP).

Le POCSAG est réglé sur DAPNET (radioamateur) : les réseaux de
radiomessagerie privés transportent des correspondances privées.

## Écrans

- **Menu** : choix du décodeur (en vert), RECEIVE, Freq, Gain (dB ou Auto),
  PPM (correction de la clé, commune à tous les décodeurs), Options du
  décodeur. Touches du haut : journal, DATV, EXIT, Config.
- **Options** : une rangée par réglage ; RTTY a deux présélections (ballons
  UKHAS 434 MHz : 50 bauds ASCII 7 bits, shift 425, USB ; HF : 45,45 bauds
  Baudot, shift 170, LSB).
- **Réception** : barre de niveau (audio en BLU, extinction du souffle en FM),
  messages du plus récent au plus ancien ; Pause (toucher la liste la fait
  défiler), Clear ou Range ou Scan, changement de vue, Menu, EXIT.
  Radar APRS : chaque station est dessinée avec son icône APRS (maison,
  digipeater, voiture… et le caractère d'overlay éventuel), son indicatif à
  côté ; radar ADS-B / VDL2 : silhouette d'avion orientée selon le cap
  (verte sous 3000 m), un point si le cap est inconnu ; radiosonde : chaîne
  de sondage (ballon, parachute, sonde) au bout de la trajectoire (cadre
  Track), la sonde sur sa position. Les étiquettes qui
  chevaucheraient une icône ou une autre étiquette sont omises.
- **FM RDS, DAB+** : nom de la station (PS, radiotexte, type de programme,
  fréquences alternatives) ou du multiplex (services, type, débit, texte en
  cours DLS), heure diffusée (à la minute) et écart de l'horloge du Pi
  (vert sous 1 s). **Set Clock** met le Pi à cette heure (`sudo date`), sauf
  s'il est synchronisé par NTP ; délais de la chaîne compensés (RDS : 1,15 s,
  mesuré). **Scan FM** relève les stations RDS de la bande (toucher une ligne
  pour l'écouter) ; **Scan Band III** cherche les multiplex DAB et règle le
  plus fort. La fréquence DAB est celle du canal (8D : 201,072 MHz).
  Toucher un service DAB+ le fait jouer sur le Pi (ligne verte ; le
  toucher à nouveau l'arrête).
- **Config** : position de la station (locator de Portsdown ou lat/lon),
  entrée HF, journal.

Le menu a deux pages de décodeurs (touche **More decoders**) : WSPR, FM RDS
et DAB+ sont sur la seconde.

## Son

Tout le son du décodeur multiple (voix numérique, écoute FM du RDS, service
DAB+ choisi, radiosonde) passe par `md_audio` : haut-parleur du Pi (`aplay`,
carte par défaut) et carte son virtuelle de Portsdown (`plughw:Loopback,0,2`,
par `aplay2`, comme le récepteur RTL-FM). Ce qui est joué sur cette carte
virtuelle est lu par `server/stream.sh` (lancé par `startup.sh` : `arecord`
puis `md_audio -w`, 32 kHz stéréo) et servi à la page web
`http://<adresse du Pi>/touchscreen/`, touche **Pi audio** (volume réglable ;
nginx relaie `/touchscreen/audio` vers le port 2007 local). C'est le seul
lecteur de la page ; il remplace l'ancien serveur 3LAS (node, ffmpeg2).

En réception, la touche **Audio** (décodeurs qui ont du son) coupe ou rend le
haut-parleur en direct, sans relancer la chaîne ; le choix est gardé
(`audio_mute`). La page web garde son propre bouton.

Radiosonde : réception `rtl_fm -M raw -F 9` (filtre FIR à la décimation),
puis chaque démodulateur de rs1729 (`--IQ 0.0 --lpIQ --dc` : filtre IQ de
7,4 kHz pour une RS41 à 24 kHz pour une M10/M20, correction de fréquence)
dans une branche de `tee` ; en mode All, RS41, M10/M20 et DFM sont décodées
en même temps. Écoute (option Audio, ON par défaut ; option Squelch OFF :
le souffle passe en permanence) : `md_fmdemod` démodule en FM le
signal IQ, avec un squelch au bruit (variance de l'écart de phase : sous
1 rad² le son passe, au-delà de 2 rad² il est coupé aussitôt) ; essai sur
signal FSK 4800 bauds simulé : ouvert dès 3 dB de rapport signal/bruit,
fermé sur le bruit seul. Le son (pas le squelch, qui mesure le signal brut)
passe par un filtre de canal de la largeur du type de sonde décodé (RS41
10 kHz, DFM 12, M10/M20 24, autres 16 ; `multidec` l'écrit dans
`/dev/shm/multidec/sonde_bw`) : essai à 6 dB de rapport signal/bruit, +11 dB
de netteté pour une RS41, +4,6 dB pour une M10.

Réglages : `scripts/multidec_config.txt` (écrit par l'application, conservé
par update.sh). Journal : `/home/pi/multidec_logs/<décodeur>_AAAAMMJJ.log`
(un avion ou un navire n'y est noté qu'à sa première réception, une
radiosonde toutes les 30 s).

## Carte web

`http://<adresse du Pi>/multidec/` : avions (silhouette orientée), navires,
stations APRS (icônes APRS, sur la carte et dans la liste), FT8 et WSPR,
radiosonde (ballon, parachute et sonde) et sa trajectoire, cercles de distance
autour de la station. Pour
les capteurs ISM 433/868, sans position, la page affiche à la place de la
carte un tableau (modèle, identifiant, valeurs, RSSI, âge, nombre de
réceptions). En FM RDS et DAB+ : la station ou le multiplex, l'heure reçue
et l'écart de l'horloge du Pi, les stations ou les services ; en DAB+, ▶
fait écouter un service dans le navigateur (flux MP3 de welle-cli, port 7979).
Aucune ressource Internet n'est utilisée : le fond de carte OpenStreetMap
(Protomaps, © contributeurs OpenStreetMap, ODbL) couvre la France jusqu'au
zoom 9 (~92 Mo : villes et routes principales, sans le détail des rues).

Fichiers servis par nginx (`scripts/configs/nginx.conf`) :
- la page : `src/multidec/web/` (planches d'icônes APRS d'aprs.fi dans
  `web/aprs/`) ;
- les positions : `/dev/shm/multidec/state.json`, écrit toutes les 2 s ;
- le fond de carte, MapLibre GL, pmtiles.js et les polices :
  `/home/pi/multidec_map/` (hors de rpidatv, conservé par les mises à jour).

`install_map.sh` télécharge ces fichiers une seule fois. L'extrait est fait par
`pmtiles_extract.py` (Python, bibliothèque standard) sur la compilation
mondiale de Protomaps la plus récente, en ne lisant que les tuiles utiles.
Autre zone ou autre détail :
`MAP_FORCE=1 MAP_MAXZOOM=10 MAP_BBOX=O,S,E,N ./install_map.sh`
(zoom 8 : ~30 Mo, 9 : ~92 Mo, 10 : ~240 Mo, 11 : ~530 Mo pour la France).

## Fonctionnement

```
rpidatvgui (Menu 5) --152--> scheduler.sh --> multidec --129/205--> rpidatvgui
multidec --> run_decoder.sh <décodeur> (groupe de processus à part)
             rtl_fm | md_level | multimon-ng ...   (sortie lue par multidec)
multidec --> /dev/shm/multidec/state.json --> nginx --> carte web
```

- `multidec.c` : écrans, lancement et arrêt de la chaîne, carte ;
  `md_dec.c` : table des décodeurs et lecture de la sortie de chaque outil ;
  `md_draw.c` : dessin (code de la GUI) ; `md_touch.c` : tactile et
  commande web (port 2005) ; `md_util.c` : config, JSON, journal.
- `md_aprs_sym.c` : icônes APRS 24 × 24 du radar, générées (sur un PC, avec
  Pillow) par `make_aprs_symbols.py` à partir du jeu de symboles d'aprs.fi
  (Heikki Hannikainen OH7LZB, https://github.com/hessu/aprs-symbols, libre
  d'usage en citant la source) ; `md_sonde_icon.c` et `web/sonde.png` :
  icône de radiosonde, dessinée en vectoriel par `make_sonde_icon.py`
  (pycairo et Pillow, sur un PC).
- `md_radio.py` : DAB+ (lecture de `mux.json` de welle-cli, changements
  seulement) et balayages FM et bande III (rtl_power, puis redsea ou
  welle-cli sur les raies trouvées).
- `md_audio` : sortie son (haut-parleur + flux web brut 16 bits, en-têtes
  X-Rate et X-Channels) ; `md_fmdemod` : écoute FM des radiosondes.
- `md_level` : recopie l'audio et mesure le niveau ; `md_ft8` : découpe
  l'audio en créneaux UTC de 15 s (7,5 s en FT4) pour decode_ft8.
- `install_decoders.sh` : compile les outils externes depuis des versions
  figées, une empreinte par outil évite toute recompilation tant que la
  version ne change pas. Tout est compilé contre la librtlsdr de Portsdown
  (`/usr/local`) ; mbelib (dsd-fme) dans un dossier privé pour ne pas
  remplacer celui de Langstone ; AIS-catcher et acarsdec sans SDRplay.

## Temps et ressources (mesurés sur Pi 4, 4 Go, Buster)

- Installation : ~10 min pour les 13 outils externes (589 s, compilations en
  parallèle selon la mémoire libre ; plus long sur un Pi de 1 Go), plus le
  téléchargement des bibliothèques de développement la première fois, plus
  ~30 s pour le fond de carte (88 Mo extraits sur le Pi) ; multidec lui-même
  se compile en 14 s (36 Mo de mémoire au plus).
- Mise à jour sans changement de version d'un outil : 2 s. Un outil dont la
  version change est seul recompilé.
- En réception (un cœur = 100 %) : multidec < 1 %, 5 Mo ; ADS-B 9 % ;
  capteurs ISM 3 % ; ACARS 8 % ; AIS 11 % ; chaînes rtl_fm (POCSAG, APRS,
  Selcall, CW, FT8, RTTY) ~20 % ; DMR 14 % ; WSPR 34 % ; radiosondes
  (RS41 + M10/M20 + DFM en parallèle) 60 % ; VDL2 (4 canaux) 60 %.

## Essais réalisés (Pi 4, Buster)

Sur signaux générés : POCSAG 512/1200/2400 (alpha et numérique), FLEX et
FLEX_NEXT, DTMF, ZVEI1, APRS (dont Mic-E) par gen_packets, RTTY ASCII 7/8
bits et Baudot (minimodem en émission), FT8 en temps réel (gen_ft8),
WSPR (auto-test de rtlsdr_wsprd).

Sur l'air (antenne au-dessus de 300 MHz) : ADS-B (25 avions, ~17 000
messages/min), capteur 433 MHz Acurite-606TX, AIS (station côtière et
navire), VDL2, APRS (F4DVK-7).

Non essayés sur l'air : radiosondes, ACARS, DMR/NXDN, FT8/WSPR en HF, CW.
