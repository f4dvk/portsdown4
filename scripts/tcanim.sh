#!/bin/bash

# TCAnim : mire animee pour l'emission DATV (source PATERNAUDIO de a.sh).
#
# Remplace tcanim1v16 de l'ancien Portsdown (dessin OpenVG, absent sur RPi 4)
# par un graphe de filtres ffmpeg, dans le meme esprit modernise :
#  - les mires video/imz-*.jpg se succedent toutes les 10 s en glissant ;
#  - des bulles "CQ" en relief, translucides, rebondissent en travers ;
#  - l'indicatif (et le locator) en haut a gauche, l'heure UTC en direct
#    et un voyant "ON AIR" clignotant en haut a droite ;
#  - un bandeau translucide fait defiler "CQ CQ CQ DE <indicatif> IN
#    <locator> - <parametres d'emission>".
# L'image est composee en 768x576 (4:3, pixels carres) puis mise a la taille
# d'emission. Le graphe doit tenir le temps reel sur un Pi 4 (ffmpeg 4.2,
# filtres sur un seul coeur) : les mires sont assemblees une fois pour toutes
# en un bandeau (mire 1, 2, 3, 1) decode une seule fois et repete en memoire
# (filtre loop), la transition n'etant qu'un recadrage glissant dans ce
# bandeau ; idem pour les bulles. Le reste n'est que copies et texte.
#
# Utilisation (source par a.sh) :
#   TCANIM_CALL, TCANIM_LOCATOR  indicatif et locator
#   TCANIM_INFO                  texte des parametres (ex. "DVB-S2 QPSK 333 kS FEC 2/3 - 437.000 MHz")
#   TCANIM_W, TCANIM_H, TCANIM_FPS  taille et cadence de sortie
#   TCANIM_IMAGES                dossier des mires imz-*.jpg
#   TCANIM_TMP                   dossier de travail (bulles, textes)
#   TCANIM_REALTIME              1 (defaut) : cadence temps reel (filtre
#                                realtime), indispensable a l'emission ; 0
#                                pour generer un fichier au plus vite
#   tcanim_prepare               -> remplit le tableau TCANIM_INPUTS (entrees
#                                   ffmpeg), TCANIM_GRAPH (-filter_complex,
#                                   sortie video [v]) et TCANIM_NEXT_INPUT
#                                   (indice de l'entree suivante, ex. le son)
# L'heure est affichee en UTC (lancer ffmpeg avec TZ=UTC), mais seulement si
# l'horloge du systeme est synchronisee : sans reseau ni module RTC, elle peut
# etre fausse. Sinon, la duree d'emission ("TX 00:12:34") est affichee a la
# place. TCANIM_CLOCK=utc ou elapsed force l'un ou l'autre.

TCANIM_FONT=${TCANIM_FONT:-/usr/share/fonts/truetype/freefont/FreeSans.ttf}
TCANIM_FONTB=${TCANIM_FONTB:-/usr/share/fonts/truetype/freefont/FreeSansBold.ttf}

tcanim_prepare()
{
  local W=768 H=576 FPS=${TCANIM_FPS:-25}
  local T=$TCANIM_TMP
  mkdir -p "$T"

  # Textes dans des fichiers (textfile=) : aucun echappement a gerer pour
  # l'indicatif ou les parametres (/, :, % ...).
  printf "%s" "$TCANIM_CALL" > "$T/tcanim_call.txt"
  printf "%s" "$TCANIM_LOCATOR" > "$T/tcanim_loc.txt"
  printf "%s" "CQ CQ CQ DE $TCANIM_CALL IN $TCANIM_LOCATOR  -  $TCANIM_INFO  -  Portsdown 4  -  " \
    > "$T/tcanim_ticker.txt"

  # Bulles "CQ" : disque de couleur assombri vers le bord (relief), reflet
  # flou en haut a gauche, 88 % d'opacite, texte blanc ombre.
  local i=0 spec d r c
  for spec in "62 #e0303a" "50 #1e90ff" "56 #f0b400"; do
    set -- $spec
    r=$1; c=$2; d=$((2*r))
    convert -size ${d}x${d} xc:none -fill "$c" -draw "circle $r,$r $r,1" \
      \( -size ${d}x${d} radial-gradient:"#00000000"-"#00000070" \) -compose Atop -composite \
      \( -size ${d}x${d} xc:none -fill "#ffffffb0" \
         -draw "ellipse $((r*62/100)),$((r*55/100)) $((r*34/100)),$((r*22/100)) 0,360" -blur 0x$((r/10)) \) \
      -compose Over -composite \
      -channel A -evaluate multiply 0.88 +channel \
      -font "$TCANIM_FONTB" -pointsize $((r*7/10)) -gravity center \
      -fill "#00000080" -annotate +2+3 "CQ" -fill white -annotate +0+1 "CQ" \
      "$T/tcanim_bulle$i.png"
    i=$((i+1))
  done

  # Horloge : heure UTC si l'horloge systeme est synchronisee, sinon duree
  # d'emission (temps de la video, depuis le lancement de ffmpeg).
  local clock="TX %{pts\\:gmtime\\:0\\:%T}"
  case "${TCANIM_CLOCK:-auto}" in
    utc) clock="%{localtime\\:%T} UTC" ;;
    elapsed) ;;
    *)
      if timedatectl show -p NTPSynchronized --value 2>/dev/null | grep -q yes \
         || timedatectl status 2>/dev/null | grep -q "synchronized: yes"; then
        clock="%{localtime\\:%T} UTC"
      fi
    ;;
  esac

  # Textes plus grands quand l'image emise est petite (352x288 aux faibles
  # debits) : ils restent lisibles une fois l'image reduite.
  local k=100
  if [ "${TCANIM_H:-576}" -lt 400 ]; then k=135; fi
  local fcall=$((54*k/100)) floc=$((26*k/100)) fclock=$((30*k/100)) fair=$((24*k/100)) ftick=$((30*k/100))
  local band=$((58*k/100))

  # Bandeau des mires (1, 2, 3 puis 1 a nouveau pour boucler sans saut),
  # chacune a la taille de composition.
  convert "$TCANIM_IMAGES/imz-10.jpg" "$TCANIM_IMAGES/imz-15.jpg" "$TCANIM_IMAGES/imz-16.jpg" \
    "$TCANIM_IMAGES/imz-10.jpg" -resize "${W}x${H}!" +append "$T/tcanim_mires.png"

  # Entrees : 0 bandeau des mires, 1-3 bulles ; une seule image chacune,
  # repetee par le filtre loop (aucun decodage a chaque image).
  TCANIM_INPUTS=(-i "$T/tcanim_mires.png")
  for i in 0 1 2; do
    TCANIM_INPUTS+=(-i "$T/tcanim_bulle$i.png")
  done
  TCANIM_NEXT_INPUT=4  # indice ffmpeg de la premiere entree ajoutee apres celles-ci (ex. le son)
  # Conversion de format faite une seule fois, avant la repetition : la
  # composition reste en YUV (format de l'encodeur), sans conversion par image.
  local rep="loop=loop=-1:size=1:start=0,setpts=N/($FPS*TB)"

  # Mires : chacune 10 s, la suivante arrive en glissant depuis la droite en
  # 1 s, par recadrage dans le bandeau (x avance d'une mire en fin de periode).
  local p="mod(t\,30)"
  local g="[0:v]format=yuv420p,$rep,crop=$W:$H:x='$W*(floor($p/10)+max(0\,mod($p\,10)-9))':y=0,setsar=1[c3];"

  # Bulles : trajectoires paraboliques de droite a gauche, en boucle
  # (montee v px/s, derive a px/s, gravite 60 px/s2, periode 2v/60, phase).
  local prev=c3 n=0 v a ph per tt
  for spec in "210 120 0" "250 160 2.7" "190 95 5.1"; do
    set -- $spec
    v=$1; a=$2; ph=$3
    per=$(LC_ALL=C awk "BEGIN {printf \"%.3f\", 2*$v/60}")  # point decimal quelle que soit la langue
    tt="mod(t+$ph\,$per)"
    g+="[$((1+n)):v]format=yuva420p,$rep[bl$n];"
    g+="[$prev][bl$n]overlay=x='W-$a*$tt-w/2':y='H-$((band+12))-($v*$tt-30*$tt*$tt)-h/2':format=yuv420[b$n];"
    prev=b$n; n=$((n+1))
  done

  # Habillage : indicatif, locator, heure UTC, ON AIR, bandeau defilant.
  g+="[$prev]"
  g+="drawtext=fontfile=$TCANIM_FONTB:textfile=$T/tcanim_call.txt:fontsize=$fcall:fontcolor=white:x=26:y=22:box=1:boxcolor=black@0.55:boxborderw=12,"
  g+="drawtext=fontfile=$TCANIM_FONT:textfile=$T/tcanim_loc.txt:fontsize=$floc:fontcolor=white:x=26:y=$((40+fcall*11/10)):box=1:boxcolor=black@0.55:boxborderw=8,"
  g+="drawtext=fontfile=$TCANIM_FONTB:text='$clock':fontsize=$fclock:fontcolor=white:x=w-tw-26:y=24:box=1:boxcolor=black@0.55:boxborderw=10,"
  g+="drawtext=fontfile=$TCANIM_FONTB:text='ON AIR':fontsize=$fair:fontcolor=white:x=w-tw-26:y=$((44+fclock*15/10)):box=1:boxcolor=0xD01010@0.9:boxborderw=8:enable='lt(mod(t\,1.2)\,0.8)',"
  g+="drawbox=x=0:y=ih-$band:w=iw:h=$band:color=black@0.6:t=fill,"
  g+="drawbox=x=0:y=ih-$((band+4)):w=iw:h=4:color=0x8000C0@1:t=fill,"
  g+="drawtext=fontfile=$TCANIM_FONT:textfile=$T/tcanim_ticker.txt:fontsize=$ftick:fontcolor=white:x='w-mod(t*90\,w+tw)':y=h-$band+($band-$ftick)/2,"
  g+="scale=${TCANIM_W:-704}:${TCANIM_H:-576}:flags=fast_bilinear,setdar=4/3,format=yuv420p"
  if [ "${TCANIM_REALTIME:-1}" == "1" ]; then g+=",realtime"; fi
  g+="[v]"
  TCANIM_GRAPH="$g"
}
