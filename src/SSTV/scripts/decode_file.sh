#!/bin/bash
# Decode une image SSTV a partir d'un fichier audio existant (.wav), sur le
# meme principe que le decodeur Sarsat 406 de Portsdown4 qui peut lire un
# fichier .wav enregistre au lieu du flux carte son.
#
# Utilisation : decode_file.sh <fichier.wav> [dossier_sortie=images] [timeout_s=30]

set -e
cd "$(dirname "$0")/.."

WAV="$1"
OUT_DIR="${2:-images}"
TIMEOUT="${3:-30}"

if [ -z "$WAV" ]; then
  echo "Usage: $0 <fichier.wav> [dossier_sortie] [timeout_s]"
  exit 1
fi

mkdir -p "$OUT_DIR"

RESULT=$(./bin/sstv_decode fichier "$WAV" "$OUT_DIR" "$TIMEOUT")
echo "$RESULT"

BMP_PATH="${RESULT#*FICHIER=}"
./scripts/view.sh "$BMP_PATH"
