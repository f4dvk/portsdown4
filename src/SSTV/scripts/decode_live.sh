#!/bin/bash
# Capture et decode les images SSTV en continu depuis une carte son, sur le
# meme principe que 406/decode.sh de Portsdown4 : sox capture le flux audio
# en continu et le transmet par pipe au decodeur. Chaque image recue est
# affichee automatiquement a l'ecran (scripts/view.sh), comme le ferait
# Portsdown4 pour une image DATV recue.
#
# Fonctionne de maniere autonome, sans Portsdown4 : il suffit d'une carte
# son ALSA (ex: interface audio USB reliee a la sortie BF du recepteur).
#
# Un futur script s'appuyant sur une cle RTL-SDR pourra remplacer la
# capture "sox -t alsa ..." par tout autre flux produisant du PCM 16 bits
# signe mono a la meme frequence d'echantillonnage, en pipant directement
# vers ./bin/sstv_decode flux <frequence_hz> <dossier_sortie>.
#
# Utilisation : decode_live.sh [carte_alsa=default] [dossier_sortie=images] [timeout_s=30]

cd "$(dirname "$0")/.."

CARTE="${1:-default}"
OUT_DIR="${2:-images}"
TIMEOUT="${3:-30}"
RATE=15000

mkdir -p "$OUT_DIR"

cleanup() {
  sudo killall -9 fbi >/dev/null 2>/dev/null
  sudo killall -9 sox >/dev/null 2>/dev/null
  exit 0
}
trap cleanup INT TERM

echo "Ecoute de la carte son '$CARTE' a ${RATE} Hz (Ctrl+C pour arreter)..."

sox -t alsa "$CARTE" -t raw -e signed-integer -b 16 -r "$RATE" -c 1 - 2>/dev/null \
  | ./bin/sstv_decode flux "$RATE" "$OUT_DIR" "$TIMEOUT" \
  | while read -r LIGNE; do
      echo "$LIGNE"
      BMP_PATH="${LIGNE#*FICHIER=}"
      ./scripts/view.sh "$BMP_PATH"
    done
