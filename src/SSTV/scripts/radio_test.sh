#!/bin/bash
# Lance RPI_SSTV avec le generateur de signal de test (sdr/rx_test.py), sans
# materiel SDR : utile pour verifier l'interface (accord, spectre, decodage)
# avant de brancher une cle RTL-SDR, un PlutoSDR ou un LimeSDR.
#
# Utilisation : radio_test.sh [fichier.wav=test_wav/test_pd120.wav]

cd "$(dirname "$0")/.."

WAV="${1:-test_wav/test_pd120.wav}"

./bin/sstv_radio --sdr test --freq 144500000 --mode FM --wav "$WAV"
