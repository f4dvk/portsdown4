#!/bin/bash
# Lance RPI_SSTV avec une cle RTL-SDR : accord, spectre et reception SSTV
# directement sur l'ecran (framebuffer).
#
# Utilisation : radio_rtlsdr.sh [frequence_hz] [mode=FM] [-- options supplementaires]
# Exemple     : radio_rtlsdr.sh 144500000 FM --touch /dev/input/event0

cd "$(dirname "$0")/.."

FREQ="${1:-144500000}"
MODE="${2:-FM}"
shift 2 2>/dev/null

./bin/sstv_radio --sdr rtlsdr --freq "$FREQ" --mode "$MODE" "$@"
