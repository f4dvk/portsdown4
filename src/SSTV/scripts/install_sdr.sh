#!/bin/bash
# Installation des dependances pour la reception SDR directe (RTL-SDR,
# PlutoSDR, LimeSDR) via bin/sstv_radio : GNU Radio et les blocs materiels
# gr-osmosdr (RTL-SDR), gr-iio (PlutoSDR) et gr-limesdr (LimeSDR).
# Optionnel -- non necessaire pour utiliser RPI_SSTV a partir d'un fichier
# audio ou d'une carte son (voir install.sh).
#
# sdr/common_rx.py cible la generation GNU Radio 3.7 / gr-osmosdr / gr-iio
# deja presente sur les installations existantes de Portsdown4 et de
# Langstone-V2 sur Raspberry Pi OS Buster -- la meme que celle-la utilise
# Langstone-V2 lui-meme pour PlutoSDR et LimeSDR. Sur cette version de
# Raspberry Pi OS, GNU Radio n'a des bindings que pour Python 2 : les
# scripts sdr/*.py sont donc ecrits en Python 2 et lances via "python2" par
# sstv_radio (voir son option --python pour changer d'interprete sur un
# systeme plus recent).
#
# Chaque module est installe separement pour qu'un paquet absent n'empeche
# pas l'installation des autres.
set -e
cd "$(dirname "$0")/.."

sudo apt-get update

echo "--- GNU Radio (coeur) ---"
sudo apt-get install -y gnuradio gnuradio-dev

echo "--- Support RTL-SDR (gr-osmosdr) ---"
sudo apt-get install -y gr-osmosdr || \
  echo "ATTENTION : paquet gr-osmosdr introuvable, RTL-SDR ne fonctionnera pas sans lui."

echo "--- Support PlutoSDR (gr-iio) ---"
sudo apt-get install -y gr-iio || \
  echo "ATTENTION : paquet gr-iio introuvable, PlutoSDR ne fonctionnera pas sans lui."

echo "--- Support LimeSDR (gr-limesdr) ---"
if ! sudo apt-get install -y gr-limesdr 2>/dev/null; then
  echo "Pas de paquet gr-limesdr sur ce systeme."
  echo "Langstone-V2 le compile lui-meme depuis les sources (meme demarche ici) :"
  echo
  echo "  sudo apt-get install -y libusb-1.0-0-dev"
  echo "  git clone https://github.com/myriadrf/LimeSuite.git ~/LimeSuite"
  echo "  cd ~/LimeSuite && mkdir -p builddir && cd builddir"
  echo "  cmake .. && make -j\$(nproc) && sudo make install && sudo ldconfig"
  echo "  git clone https://github.com/myriadrf/gr-limesdr.git ~/gr-limesdr"
  echo "  cd ~/gr-limesdr && mkdir -p build && cd build"
  echo "  cmake .. && make -j\$(nproc) && sudo make install && sudo ldconfig"
  echo
  echo "(non fait automatiquement ici : compilation longue, a lancer a la main si besoin)"
fi

./scripts/build.sh

echo
echo "Verification rapide (ne doit afficher aucune ModuleNotFoundError) :"
python2 -c "import osmosdr" 2>&1 | tail -1
python2 -c "from gnuradio import iio" 2>&1 | tail -1
python2 -c "import limesdr" 2>&1 | tail -1

echo
echo "Test sans materiel (genere un signal de synthese) :"
echo "  ./scripts/radio_test.sh"
