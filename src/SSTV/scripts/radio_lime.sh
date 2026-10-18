#!/bin/bash
# Lance RPI_SSTV avec un LimeSDR / LimeSDR Mini : accord, spectre et
# reception SSTV directement sur l'ecran (framebuffer).
#
# Utilisation : radio_lime.sh [frequence_hz] [mode=FM] [-- options supplementaires]

cd "$(dirname "$0")/.."

FREQ="${1:-144500000}"
MODE="${2:-FM}"
shift 2 2>/dev/null

./bin/sstv_radio --sdr lime --freq "$FREQ" --mode "$MODE" "$@"
