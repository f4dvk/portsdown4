#!/bin/bash
# Installation des dependances sur Raspberry Pi OS (Raspbian) Lite, puis
# compilation. A executer une seule fois.
set -e
cd "$(dirname "$0")/.."

sudo apt-get update
sudo apt-get install -y build-essential sox fbi

mkdir -p bin images

./scripts/build.sh

echo
echo "Installation terminee."
echo "Test rapide sur un fichier fourni :"
echo "  ./scripts/gui_file.sh test_wav/test_pd120.wav      (affichage framebuffer)"
echo "  ./scripts/decode_file.sh test_wav/test_pd120.wav   (affichage via fbi)"
echo
echo "Pour la reception directe RTL-SDR / PlutoSDR / LimeSDR (accord, spectre,"
echo "bin/sstv_radio), executer en plus :"
echo "  ./scripts/install_sdr.sh"
