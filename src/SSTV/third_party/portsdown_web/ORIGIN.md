# Origine de ce code

`ffunc.c` et `ffunc.h` sont copiés tels quels depuis `src/gui/ffunc.c` /
`src/gui/ffunc.h` de ce même dépôt Portsdown4, sous licence **GPLv3** (voir
`LICENSE` à la racine du dépôt Portsdown4).

C'est la mini-bibliothèque FastCGI partagée par toutes les applications
framebuffer de Portsdown4 (`bandview`, `power_meter`, `dmm`, `rydemon`,
`src/gui/rpidatvtouch4.c`, ...) pour recevoir la "commande web" : nginx
(`scripts/configs/nginx.conf`) relaie les `POST /touchscreen/click` d'une
page web (`scripts/configs/webroot/touchscreen/index.html`, qui affiche
`screen.png` dans un `<canvas>` et poste les coordonnées du clic) vers
`127.0.0.1:2005` en FastCGI, et `ffunc_run()` résout dynamiquement
(`dlsym`, d'où le lien `-rdynamic -ldl`) le handler `touchscreenClick`
défini dans le binaire de l'application elle-même.

## Utilisation dans RPI_SSTV

`src/sstv_radio.cpp` réutilise ce mécanisme sans aucune modification de
`ffunc.c`/`ffunc.h` : il définit sa propre fonction `touchscreenClick()`
(en `extern "C"`, obligatoire pour que `dlsym` retrouve le symbole non
"manglé" par g++) qui traduit le clic web en un point `touchX`/`touchY`
traité exactement comme un appui tactile physique (voir `handle_touch_point()`
dans `sstv_radio.cpp`).

La capture périodique de l'écran en `screen.png` (affichée par la page web)
n'est **pas** faite par `sstv_radio` : comme pour toutes les applications
Portsdown4, c'est le script externe `scripts/screen_grab_for_web.sh` (lancé
par `scheduler.sh` juste avant l'application) qui s'en charge, via
`raspi2png`.

## Activation

Comme pour `rydemon`/`bandview`/etc., la commande web est **désactivée par
défaut** : `sstv_radio` ne démarre le thread d'écoute FastCGI que si
`webcontrol=enabled` est présent dans
`/home/pi/rpidatv/scripts/portsdown_config.txt` (fichier de config commun à
Portsdown4), ou si l'option `--web` est passée explicitement en ligne de
commande (utile pour tester hors d'une installation Portsdown4, où ce
fichier de config n'existe pas).
