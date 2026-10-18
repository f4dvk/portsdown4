#!/bin/bash
# Capture continue depuis une carte son et affichage direct sur le
# framebuffer (technique Langstone-V2), sans passer par fbi : chaque image
# est dessinee ligne par ligne au fur et a mesure de sa reception.
#
# Utilisation : gui_live.sh [carte_alsa=default] [dossier_sortie=images] [timeout_s=30] [--touch /dev/input/eventX]

cd "$(dirname "$0")/.."

CARTE="default"
OUT_DIR="images"
TIMEOUT="30"

if [ -n "$1" ] && [[ "$1" != --* ]]; then CARTE="$1"; shift; fi
if [ -n "$1" ] && [[ "$1" != --* ]]; then OUT_DIR="$1"; shift; fi
if [ -n "$1" ] && [[ "$1" != --* ]]; then TIMEOUT="$1"; shift; fi

RATE=15000
mkdir -p "$OUT_DIR"

cleanup() {
  sudo killall -9 sox >/dev/null 2>/dev/null
  exit 0
}
trap cleanup INT TERM

echo "Ecoute de la carte son '$CARTE' a ${RATE} Hz (Ctrl+C ou bouton Quitter pour arreter)..."

sox -t alsa "$CARTE" -t raw -e signed-integer -b 16 -r "$RATE" -c 1 - 2>/dev/null \
  | ./bin/sstv_gui flux "$RATE" "$OUT_DIR" "$TIMEOUT" "$@"
