#!/bin/bash
# install_map.sh : fichiers de la carte web du decodeur multiple, servis par
# nginx sous http://<Pi>/multidec/map/ (dossier hors de rpidatv, conserve
# par les mises a jour) :
#  - bibliotheques d'affichage MapLibre GL et pmtiles (versions figees) ;
#  - polices des noms de lieux (Noto Sans, caracteres latins) ;
#  - fond de carte de la France, OpenStreetMap par Protomaps, jusqu'au
#    zoom 9 (~92 Mo : villes, routes principales, sans le detail des rues),
#    extrait par pmtiles_extract.py d'une compilation recente du monde.
# Chaque element deja present n'est pas retelecharge. MAP_FORCE=1 refait
# l'extrait ; MAP_MAXZOOM et MAP_BBOX permettent un autre extrait.

DIR=/home/pi/multidec_map
SRC=/home/pi/rpidatv/src/multidec
LOGS=/home/pi/build_logs/multidec
MAXZOOM=${MAP_MAXZOOM:-9}
BBOX=${MAP_BBOX:--5.8,41.2,10.2,51.4}       # France metropolitaine et Corse
V_MAPLIBRE=4.7.1
V_PMTILES=3.2.1

mkdir -p "$DIR" "$LOGS"
cd "$DIR" || exit 1
FAILED=""

get()   # url fichier
{
  [ -s "$2" ] && return 0
  wget -q -O "$2.part" "$1" && mv -f "$2.part" "$2" || { rm -f "$2.part"; FAILED="$FAILED $2"; }
}

get https://unpkg.com/maplibre-gl@$V_MAPLIBRE/dist/maplibre-gl.js maplibre-gl.js
get https://unpkg.com/maplibre-gl@$V_MAPLIBRE/dist/maplibre-gl.css maplibre-gl.css
get https://unpkg.com/pmtiles@$V_PMTILES/dist/pmtiles.js pmtiles.js
mkdir -p "fonts/Noto Sans Regular"
for r in 0-255 256-511 8192-8447; do
  get "https://protomaps.github.io/basemaps-assets/fonts/Noto%20Sans%20Regular/$r.pbf" "fonts/Noto Sans Regular/$r.pbf"
done

if [ ! -s france.pmtiles ] || [ "$MAP_FORCE" == "1" ]; then
  # compilation du monde la plus recente (Protomaps en garde quelques jours)
  URL=""
  for d in 1 2 3 4 5 6 7 8 9 10; do
    U="https://build.protomaps.com/$(date -d "-$d day" +%Y%m%d).pmtiles"
    if wget -q --spider "$U"; then URL=$U; break; fi
  done
  if [ -z "$URL" ]; then
    FAILED="$FAILED france.pmtiles"
  else
    start=$(date +%s)
    if python3 "$SRC/pmtiles_extract.py" "$URL" france.pmtiles --bbox="$BBOX" --maxzoom "$MAXZOOM" \
        > "$LOGS/map.log" 2>&1; then
      echo "Fond de carte : $(du -m france.pmtiles | cut -f1) Mo en $(( $(date +%s) - start )) s"
    else
      FAILED="$FAILED france.pmtiles"
    fi
  fi
fi

if [ -n "$FAILED" ]; then
  echo "Carte : non installe :$FAILED (voir $LOGS/map.log)"
  exit 1
fi
exit 0
