#!/bin/bash
# Decode un fichier audio et affiche l'image directement sur le framebuffer
# (technique Langstone-V2 : mmap de /dev/fb0, sans serveur X), ligne par
# ligne au fur et a mesure du decodage.
#
# Utilisation : gui_file.sh <fichier.wav> [dossier_sortie=images] [timeout_s=30] [--touch /dev/input/eventX]

cd "$(dirname "$0")/.."

if [ -z "$1" ]; then
  echo "Usage: $0 <fichier.wav> [dossier_sortie] [timeout_s] [--touch /dev/input/eventX]"
  exit 1
fi

WAV="$1"; shift
OUT_DIR="images"
TIMEOUT="30"

if [ -n "$1" ] && [[ "$1" != --* ]]; then OUT_DIR="$1"; shift; fi
if [ -n "$1" ] && [[ "$1" != --* ]]; then TIMEOUT="$1"; shift; fi

mkdir -p "$OUT_DIR"

./bin/sstv_gui fichier "$WAV" "$OUT_DIR" "$TIMEOUT" "$@"
