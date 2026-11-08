#!/bin/bash
# install_decoders.sh : compile et installe les decodeurs externes utilises par
# le decodeur multiple (multidec). Appele par install_portsdown.sh et update.sh.
#
#  - Chaque outil est compile depuis une version figee ; une empreinte par
#    outil (/usr/local/share/multidec/stamps) evite de le recompiler tant que
#    la version ne change pas : une mise a jour ordinaire ne prend que
#    quelques secondes ici.
#  - Tout est compile contre la librtlsdr de Portsdown (/usr/local) : ne
#    jamais installer les paquets librtlsdr-dev / rtl-sdr de Raspbian.
#  - mbelib (pour dsd-fme) est compile dans un dossier prive, pour ne pas
#    remplacer celui de /usr/local utilise par Langstone.
#  - Journaux : /home/pi/build_logs/multidec/<outil>.log
#
# Usage : install_decoders.sh [outil ...]   (sans argument : tous)
#         MULTIDEC_FORCE=1 install_decoders.sh   (tout recompiler)

BUILD=/home/pi/multidec_build
LOGS=/home/pi/build_logs/multidec
STAMPS=/usr/local/share/multidec/stamps
PRIV=/usr/local/lib/multidec          # mbelib prive

# Versions figees (etiquettes git, ou commit pour les depots sans etiquette)
V_MULTIMON=1.6.2
V_DIREWOLF=1.8.1
V_RTL433=25.12
V_READSB=v3.16.21
V_RS=89b1ea7f3371d99c630f6734c42014d88e667853-2
V_AISCATCHER=v0.70
R_AISCATCHER=$V_AISCATCHER-rtl     # empreinte : version + options de compilation
V_LIBACARS=v2.2.1
V_DUMPVDL2=v2.7.0
V_ACARSDEC=v4.7
R_ACARSDEC=$V_ACARSDEC-json2     # avec cJSON (sortie JSON)
V_MBELIB=v1.3.0
V_DSDFME=72f8c1aff7e223d6599db597e6bca9bd11ea4b92
V_FT8LIB=2.0
V_WSPRD=0.5.7
V_REDSEA=v1.3.1
V_WELLE=v2.7

ALL="minimodem multimon direwolf rtl433 readsb rs aiscatcher libacars dumpvdl2 acarsdec dsdfme ft8lib wsprd redsea welle"

mkdir -p "$BUILD" "$LOGS"
sudo mkdir -p "$STAMPS" "$PRIV"
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

# Compilations paralleles : au plus un coeur par tranche de 350 Mo disponibles
# (les fichiers C++ d'AIS-catcher sont les plus gourmands)
JOBS=$(( $(awk '/MemAvailable/ {print int($2/1024)}' /proc/meminfo) / 350 ))
if [ "$JOBS" -gt "$(nproc)" ]; then JOBS=$(nproc); fi
if [ "$JOBS" -lt 1 ]; then JOBS=1; fi

stamp_ok()   # outil version
{
  [ "$MULTIDEC_FORCE" != "1" ] && [ "$(cat "$STAMPS/$1" 2>/dev/null)" == "$2" ]
}

stamp_set()  # outil version
{
  echo "$2" | sudo tee "$STAMPS/$1" >/dev/null
}

# Clone superficiel d'une etiquette ou d'un commit dans $BUILD/<dossier>
fetch()      # depot dossier version
{
  rm -rf "${BUILD:?}/$2"
  mkdir -p "$BUILD/$2" && cd "$BUILD/$2" || return 1
  git init -q . && git remote add origin "https://github.com/$1.git" \
    && git fetch -q --depth 1 origin "$3" && git checkout -q FETCH_HEAD
}

cmake_build()   # options cmake...
{
  mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release "$@" .. && make -j"$JOBS"
}

# ------------------------------------------------------------------------------

b_minimodem()
{
  # RTTY (Baudot, ASCII) : la 0.24 de Raspbian est la derniere version publiee
  stamp_ok minimodem apt && return 0
  sudo apt-get -y install minimodem && stamp_set minimodem apt
}

b_multimon()
{
  stamp_ok multimon $V_MULTIMON && return 0
  fetch EliasOenal/multimon-ng multimon-ng $V_MULTIMON \
    && cmake_build -DCMAKE_DISABLE_FIND_PACKAGE_X11=ON -DCMAKE_DISABLE_FIND_PACKAGE_PulseAudio=ON \
    && sudo make install && stamp_set multimon $V_MULTIMON
}

b_direwolf()
{
  stamp_ok direwolf $V_DIREWOLF && return 0
  fetch wb2osz/direwolf direwolf $V_DIREWOLF && cmake_build \
    && sudo make install && stamp_set direwolf $V_DIREWOLF
}

b_rtl433()
{
  stamp_ok rtl433 $V_RTL433 && return 0
  fetch merbanan/rtl_433 rtl_433 $V_RTL433 \
    && cmake_build -DENABLE_SOAPYSDR=OFF -DENABLE_OPENSSL=OFF -DBUILD_TESTING=OFF \
    && sudo make install && stamp_set rtl433 $V_RTL433
}

b_readsb()
{
  stamp_ok readsb $V_READSB && return 0
  fetch wiedehopf/readsb readsb $V_READSB \
    && make -j"$JOBS" RTLSDR=yes OPTIMIZE="-O2" \
    && sudo cp readsb /usr/local/bin/readsb && stamp_set readsb $V_READSB
}

b_rs()
{
  # Radiosondes : demodulateurs de rs1729 (ceux qu'utilise radiosonde_auto_rx)
  local m ok=0
  stamp_ok rs $V_RS && return 0
  fetch rs1729/RS RS "${V_RS%-*}" || return 1
  cd demod/mod || return 1
  gcc -O2 -c demod_mod.c && gcc -O2 -c bch_ecc_mod.c || return 1
  for m in rs41mod dfm09mod m10mod m10m20mod mXXmod rs92mod lms6Xmod meisei100mod imet54mod mp3h1mod mts01mod; do
    if [ -f $m.c ] && gcc -O2 $m.c demod_mod.o bch_ecc_mod.o -lm -o $m; then
      sudo cp $m /usr/local/bin/$m
      ok=$((ok + 1))
    else
      echo "Echec : $m"
    fi
  done
  cd ../../scan && gcc -O2 dft_detect.c -lm -o dft_detect && sudo cp dft_detect /usr/local/bin/ || return 1
  [ "$ok" -ge 4 ] && stamp_set rs $V_RS
}

b_aiscatcher()
{
  # RTL-SDR seulement : avec SDRplay, AIS-catcher s'arrete au demarrage si
  # l'API SDRplay (installee par Portsdown) ne tourne pas
  stamp_ok aiscatcher $R_AISCATCHER && return 0
  fetch jvde-github/AIS-catcher AIS-catcher $V_AISCATCHER \
    && cmake_build -DSDRPLAY=OFF -DAIRSPY=OFF -DAIRSPYHF=OFF -DHACKRF=OFF -DHYDRASDR=OFF -DPSQL=OFF \
         -DZMQ=OFF -DNMEA2000=OFF -DWEBVIEWER=OFF -DOPENSSL=OFF \
    && sudo cp AIS-catcher /usr/local/bin/AIS-catcher && stamp_set aiscatcher $R_AISCATCHER
}

b_libacars()
{
  stamp_ok libacars $V_LIBACARS && return 0
  fetch szpajder/libacars libacars $V_LIBACARS && cmake_build \
    && sudo make install && sudo ldconfig && stamp_set libacars $V_LIBACARS
}

b_dumpvdl2()
{
  stamp_ok dumpvdl2 $V_DUMPVDL2 && return 0
  fetch szpajder/dumpvdl2 dumpvdl2 $V_DUMPVDL2 && cmake_build \
    && sudo make install && stamp_set dumpvdl2 $V_DUMPVDL2
}

b_acarsdec()
{
  stamp_ok acarsdec $R_ACARSDEC && return 0
  # Sans SDRplay (l'API installee par Portsdown est plus ancienne que celle
  # qu'attend acarsdec), ni Airspy, SoapySDR ou MQTT : RTL-SDR seulement
  # cJSON de Raspbian Buster (1.7.10) sans fichier pkg-config : on en ecrit un
  mkdir -p "$BUILD/pc"
  printf "Name: libcjson\nDescription: cJSON\nVersion: 1.7.10\nLibs: -lcjson\nCflags: -I/usr/include/cjson\n" \
    > "$BUILD/pc/libcjson.pc"
  export PKG_CONFIG_PATH="$BUILD/pc:$PKG_CONFIG_PATH"
  fetch f00b4r0/acarsdec acarsdec $V_ACARSDEC \
    && cmake_build -DSDRPLAY=OFF -DAIRSPY=OFF -DSOAPYSDR=OFF -DMQTT=OFF \
    && sudo make install && stamp_set acarsdec $R_ACARSDEC
}

b_dsdfme()
{
  stamp_ok dsdfme $V_DSDFME && return 0
  # mbelib prive (statique), puis dsd-fme lie dessus
  fetch lwvmobile/mbelib mbelib $V_MBELIB \
    && cmake_build -DCMAKE_INSTALL_PREFIX="$PRIV" -DBUILD_SHARED_LIBS=OFF \
    && sudo make install || return 1
  # gcc 8 refuse une etiquette en fin de bloc (permis depuis C23) : ";" ajoute
  fetch lwvmobile/dsd-fme dsd-fme $V_DSDFME \
    && perl -0pi -e 's/(\n[ \t]*[A-Za-z_]\w*:)([ \t]*(\/\/[^\n]*)?)(\s*\n[ \t]*\})/$1;$2$4/g' src/*.c \
    && cmake_build -DCMAKE_PREFIX_PATH="$PRIV" -DMBE_INCLUDE_DIR="$PRIV/include" \
         -DMBE_LIBRARY="$PRIV/lib/libmbe.a" \
    && sudo cp dsd-fme /usr/local/bin/dsd-fme && stamp_set dsdfme $V_DSDFME
}

b_ft8lib()
{
  stamp_ok ft8lib $V_FT8LIB && return 0
  # Sans -fsanitize=address (lent) ; gnu11 pour clock_gettime/gmtime_r
  fetch kgoba/ft8_lib ft8_lib $V_FT8LIB \
    && make -j"$JOBS" CFLAGS="-O2" CPPFLAGS="-std=gnu11 -I." LDFLAGS="-lm" decode_ft8 \
    && sudo cp decode_ft8 /usr/local/bin/decode_ft8 && stamp_set ft8lib $V_FT8LIB
}

b_wsprd()
{
  stamp_ok wsprd $V_WSPRD && return 0
  fetch Guenael/rtlsdr-wsprd rtlsdr-wsprd $V_WSPRD && make -j"$JOBS" \
    && sudo cp rtlsdr_wsprd /usr/local/bin/rtlsdr_wsprd && stamp_set wsprd $V_WSPRD
}

b_redsea()
{
  # RDS (FM) : meson de buster-backports ; nlohmann_json telecharge par meson
  # (celui de Buster, 3.5, est trop ancien)
  stamp_ok redsea $V_REDSEA && return 0
  fetch windytan/redsea redsea $V_REDSEA \
    && meson setup build --buildtype=release && ninja -C build -j "$JOBS" \
    && sudo cp build/redsea /usr/local/bin/redsea && stamp_set redsea $V_REDSEA
}

b_welle()
{
  # DAB+ : welle-cli seul (sans l'interface Qt), RTL-SDR
  stamp_ok welle $V_WELLE && return 0
  # La page web de welle-cli est integree avec "xxd -i -n <nom> <entree>
  # <sortie>" ; le xxd de Buster (1998) n'a pas -n, mais son "xxd -i" nomme
  # deja la variable d'apres le fichier (index.html : index_html)
  mkdir -p "$BUILD/bin"
  cat > "$BUILD/bin/xxd" <<'XXD'
#!/bin/bash
if [ "$1" == "-i" ] && [ "$2" == "-n" ]; then
  out=$5; case $out in /*) ;; *) out="$PWD/$out" ;; esac
  (cd "$(dirname "$4")" && /usr/bin/xxd -i "$(basename "$4")") > "$out"
else
  exec /usr/bin/xxd "$@"
fi
XXD
  chmod 755 "$BUILD/bin/xxd"
  fetch AlbrechtL/welle.io welle.io $V_WELLE \
    && cmake_build -DBUILD_WELLE_IO=OFF -DBUILD_WELLE_CLI=ON -DRTLSDR=ON -DSOAPYSDR=OFF -DAIRSPY=OFF \
         -DRESOURCE_COMPILER="$BUILD/bin/xxd" \
    && sudo make install && stamp_set welle $V_WELLE
}

# ------------------------------------------------------------------------------

TOOLS=${*:-$ALL}

# Bibliotheques de developpement manquantes seulement (verification dpkg,
# rapide ; pas de librtlsdr-dev : celle de Portsdown est dans /usr/local)
DEPS="libpulse-dev libcodec2-dev libjson-c-dev libzmq3-dev libprotobuf-c-dev libjansson-dev libcjson-dev
      meson ninja-build libliquid-dev libsndfile1-dev libfaad-dev libmpg123-dev libmp3lame-dev libfftw3-dev
      libasound2-dev"
MISSING=""
for p in $DEPS; do
  dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "install ok installed" || MISSING="$MISSING $p"
done
if [ -n "$MISSING" ]; then
  sudo apt-get -y install $MISSING > "$LOGS/apt.log" 2>&1
fi

FAILED=""
for t in $TOOLS; do
  start=$(date +%s)
  ( b_$t ) > "$LOGS/$t.log" 2>&1
  rc=$?
  echo "$t : $([ $rc == 0 ] && echo ok || echo ECHEC) ($(( $(date +%s) - start )) s)"
  if [ $rc != 0 ]; then FAILED="$FAILED $t"; fi
done
sudo ldconfig
rm -rf "$BUILD"

if [ -n "$FAILED" ]; then
  echo "Decodeurs non installes :$FAILED (voir $LOGS)"
  exit 1
fi
exit 0
