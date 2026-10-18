#!/bin/bash
# Compile le decodeur SSTV (avec et sans interface ecran). A relancer apres
# toute modification des sources.
set -e
cd "$(dirname "$0")/.."

SSTV_LIB="third_party/sstv_library/sstv_decoder.cpp \
          third_party/sstv_library/cordic.cpp \
          third_party/sstv_library/half_band_filter2.cpp \
          third_party/sstv_library/bmp_lib.cpp"

g++ -O2 -Wall -std=c++17 \
  src/sstv_decode.cpp src/wav_reader.cpp $SSTV_LIB \
  -o bin/sstv_decode

g++ -O2 -Wall -std=c++17 \
  src/sstv_gui.cpp src/wav_reader.cpp $SSTV_LIB \
  -o bin/sstv_gui

# sstv_radio inclut la "commande web" de Portsdown4 (third_party/
# portsdown_web/ffunc.c) : necessite libfcgi-dev (-lfcgi), et -rdynamic
# -ldl -lpthread pour que ffunc_run() retrouve touchscreenClick() par son
# nom (dlsym) dans le binaire lui-meme.
g++ -O2 -Wall -std=c++17 -rdynamic \
  src/sstv_radio.cpp $SSTV_LIB third_party/portsdown_web/ffunc.c \
  -lfcgi -ldl -lpthread \
  -o bin/sstv_radio

echo "Compilation OK : bin/sstv_decode (sans ecran), bin/sstv_gui (framebuffer),"
echo "                 bin/sstv_radio (RTL-SDR/PlutoSDR/LimeSDR + framebuffer)"
