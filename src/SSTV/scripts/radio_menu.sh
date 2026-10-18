#!/bin/bash
# Lance RPI_SSTV sans choisir le SDR a l'avance : un menu tactile propose
# RTL-SDR / PlutoSDR / LimeSDR / Test. Necessite un ecran tactile (--touch).
#
# Utilisation : radio_menu.sh --touch /dev/input/event0 [frequence_hz] [mode]

cd "$(dirname "$0")/.."
./bin/sstv_radio "$@"
