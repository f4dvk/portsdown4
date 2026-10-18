# Origine de ce code

`Graphics.h`, `Font.h` et `Touch.h` proviennent de
[Langstone-V2](https://github.com/g4eml/Langstone-V2) par Colin Durbridge
G4EML, sous licence **GPLv3** (voir `LICENSE.upstream`, texte complet et
inchangé de la licence du projet d'origine).

C'est la même technique de gestion d'écran que Langstone : accès direct au
framebuffer Linux (`mmap` de `/dev/fb0`), dessin pixel par pixel (texte,
boutons, lignes) sans serveur X, et lecture directe des événements tactiles
sur `/dev/input/eventX`.

## Modifications apportées

- `Graphics.h` :
  - `setPixel()` limitait les coordonnées en dur à `x<800 && y<480` (la
    résolution de l'écran tactile 7" utilisé par Langstone). Ceci a été
    remplacé par un test sur `screenXsize`/`screenYsize`, lus dynamiquement,
    pour ne pas dépendre d'une résolution d'écran particulière.
  - `setPixel()` supposait un framebuffer 32 bits/pixel (BGRA). Le support
    des framebuffers 16 bits/pixel (RGB565, fréquent sur les petits écrans
    SPI/TFT) et 24 bits/pixel a été ajouté.
  - `initScreen()` a été complétée par `initScreenAt(device, largeur,
    hauteur, bpp)`, qui permet d'imposer la géométrie sans passer par les
    `ioctl` du pilote framebuffer. Cela sert uniquement à valider le rendu
    sur un fichier ordinaire pendant le développement, en dehors d'un
    Raspberry Pi ; `initScreen()` (sans argument) garde le comportement
    d'origine sur `/dev/fb0`.
- `Touch.h` : `initTouch()` ne renvoyait pas explicitement `0` en cas de
  succès (fonction C sans `return` sur le chemin normal). Un `return 0;` a
  été ajouté pour que RPI_SSTV puisse désactiver proprement le tactile si le
  périphérique indiqué ne s'ouvre pas, au lieu de le supposer actif.
- `Font.h` : recopié à l'identique (table de police bitmap, aucune
  modification).

## Technique de spectre/cascade et détection tactile

`src/sstv_radio.cpp` reprend aussi, en les réécrivant (pas de code copié),
deux techniques observées dans `LangstoneGUI_RtlSdr.c` d'une version étendue
de Langstone-V2 avec support RTL-SDR, présente dans le dépôt Portsdown4 de
l'utilisateur (`portsdown4/src/langstone/`, même licence GPLv3) :

- la fonction `waterfall()` : une trace spectrale (dernier relevé FFT,
  affiché brut pour rester réactif) au-dessus d'une cascade qui accumule
  l'historique ligne par ligne, avec la même palette de couleurs
  (`gen_palette`, noir→bleu→vert→jaune→rouge) ;
- la fonction `detectHw()` : détection automatique de l'écran tactile en
  cherchant un contrôleur connu (FT5406/pi-ts, MPI7003, Goodix) dans
  `/proc/bus/input/devices`, pour éviter de deviner `/dev/input/eventX` à la
  main.

## Implication sur la licence

La GPLv3 étant une licence à copyleft fort, le programme qui combine ce code
(`bin/sstv_gui`, dans `src/sstv_gui.cpp`) est nécessairement distribué sous
GPLv3 dans son ensemble — voir le fichier `LICENSE` à la racine du dépôt.
Le décodeur en ligne de commande sans écran (`bin/sstv_decode`), qui
n'inclut pas ces fichiers, reste sous licence MIT.
