#!/bin/bash
# Lance RPI_SSTV avec un Adalm PlutoSDR : accord, spectre et reception SSTV
# directement sur l'ecran (framebuffer).
#
# Utilisation : radio_pluto.sh [frequence_hz] [mode=FM] [-- options supplementaires]
# Exemple     : radio_pluto.sh 144500000 FM --device-args "uri=ip:192.168.2.1"

cd "$(dirname "$0")/.."

FREQ="${1:-144500000}"
MODE="${2:-FM}"
shift 2 2>/dev/null

./bin/sstv_radio --sdr pluto --freq "$FREQ" --mode "$MODE" "$@"
