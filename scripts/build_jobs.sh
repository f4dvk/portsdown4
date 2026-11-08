#!/bin/bash
# build_jobs.sh : compilation des programmes Portsdown, partagee par update.sh et
# install_portsdown.sh (a sourcer apres la copie des sources dans rpidatv).
#
#  - Les programmes independants sont compiles en parallele : au plus un par
#    coeur, et dans un budget memoire (memoire disponible moins une marge).
#    Chaque compilation est declaree avec sa memoire de pointe, mesuree sur un
#    Pi 4 (leandvb 131 Mo, avc2ts 100, SSTV 71, GUI 65, les autres < 45) et
#    majoree d'un tiers : un Pi 4 de 1 Go compile sans saturer sa memoire.
#  - Un programme dont les sources, le compilateur et les bibliotheques
#    (LimeSuite, rtl-sdr) n'ont pas change depuis la derniere compilation, et
#    dont le binaire installe est intact, n'est pas recompile. Supprimer
#    /home/pi/rpidatv/.build_sums (ou PORTSDOWN_FULL_BUILD=1) force tout.
#  - Journaux de compilation : /home/pi/build_logs/<programme>.log
#
# Essai sans rien installer dans le systeme : BJ_HOME=<copie> BJ_SYS=0

BJ_HOME=${BJ_HOME:-/home/pi}
BJ_SYS=${BJ_SYS:-1}
S=$BJ_HOME/rpidatv/src
B=$BJ_HOME/rpidatv/bin
BUILD_SUMS=$BJ_HOME/rpidatv/.build_sums
BUILD_LOGS=$BJ_HOME/build_logs

# ------------------------------------------------------------------------------
# Ressources : coeurs et memoire disponible (Mo)

bj_resources()
{
  BUILD_CPUS=${BJ_CPUS:-$(nproc)}
  BUILD_BUDGET_MB=$(( $(awk '/MemAvailable/ {print int($2/1024)}' /proc/meminfo) - 150 ))
  if [ -n "$BJ_BUDGET_MB" ]; then      # essai : simuler un Pi avec moins de memoire
    BUILD_BUDGET_MB=$BJ_BUDGET_MB
  fi
  if [ "$BUILD_BUDGET_MB" -lt 200 ]; then
    BUILD_BUDGET_MB=200
  fi
}

# ------------------------------------------------------------------------------
# Empreintes : ne recompiler que ce qui a change

# Empreinte des sources (dossiers ou fichiers), du compilateur et des
# bibliotheques dont dependent les programmes
src_sum()
{
  {
    find "$@" -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.cc' -o -name '*.h' \
      -o -name '*.hpp' -o -name '*.S' -o -name 'Makefile' -o -name 'makefile' -o -name '*.mk' \
      -o -name 'CMakeLists.txt' -o -name '*.sh' \) -not -path '*/CMakeFiles/*' -print0 \
      | sort -z | xargs -0 -r sha1sum
    gcc --version | head -1
    cat /home/pi/LimeSuite/commit_tag.txt 2>/dev/null
    ls -l /usr/local/lib/librtlsdr.so.* 2>/dev/null | awk '{print $NF, $5}'
  } | sha1sum | cut -c1-40
}

# Vrai si la derniere compilation avait ces sources et que les fichiers
# produits ($3...) sont toujours ceux qu'elle avait installes
build_skip()
{
  local name=$1 sum=$2 f=$BUILD_SUMS/$1 b
  shift 2
  [ -f "$f" ] || return 1
  for b in "$@"; do
    [ -f "$b" ] || return 1
  done
  [ "$(cat "$f")" == "$(printf '%s\n' "$sum"; sha1sum "$@" | cut -c1-40)" ] || return 1
  echo "$name : sources inchangees, pas de recompilation"
  return 0
}

build_remember()
{
  local name=$1 sum=$2
  shift 2
  mkdir -p "$BUILD_SUMS"
  { printf '%s\n' "$sum"; sha1sum "$@" | cut -c1-40; } > "$BUILD_SUMS/$name"
}

# Installe un fichier dans un dossier : copie temporaire puis renommage, ce qui
# remplace aussi un binaire appartenant a root (anciens "sudo make install") ou
# en cours d'execution
bj_put()   # fichier, dossier
{
  local name
  name=$(basename "$1")
  cp "$1" "$2/.$name.new" && mv -f "$2/.$name.new" "$2/$name"
}

# Copie dans le systeme (sudo), sauf en essai
sys()
{
  if [ "$BJ_SYS" == "1" ]; then
    sudo "$@"
  fi
}

# ------------------------------------------------------------------------------
# File de compilations paralleles

PJ_NAMES=()
PJ_MEM=()
PJ_FUNCS=()

pj_add()   # nom, memoire de pointe (Mo), fonction
{
  PJ_NAMES+=("$1")
  PJ_MEM+=("$2")
  PJ_FUNCS+=("$3")
}

pj_run()   # lance la file ; retourne le nombre d'echecs
{
  local i=0 n=${#PJ_NAMES[@]} used=0 fails=0 pid rc
  local -A pmem pname pstart
  mkdir -p "$BUILD_LOGS"
  bj_resources
  echo "Compilation de $n programmes : $BUILD_CPUS en parallele au plus, budget memoire $BUILD_BUDGET_MB Mo"
  while [ $i -lt $n ] || [ ${#pmem[@]} -gt 0 ]; do
    # Lancer tant qu'il reste un coeur et de la memoire (une compilation seule
    # part toujours, meme au-dela du budget)
    while [ $i -lt $n ] && [ ${#pmem[@]} -lt $BUILD_CPUS ] \
      && { [ ${#pmem[@]} -eq 0 ] || [ $(( used + PJ_MEM[i] )) -le $BUILD_BUDGET_MB ]; }; do
      ( ${PJ_FUNCS[i]} ) > "$BUILD_LOGS/${PJ_NAMES[i]}.log" 2>&1 &
      pid=$!
      pmem[$pid]=${PJ_MEM[i]}
      pname[$pid]=${PJ_NAMES[i]}
      pstart[$pid]=$SECONDS
      used=$(( used + PJ_MEM[i] ))
      i=$(( i + 1 ))
    done
    sleep 0.5
    for pid in "${!pmem[@]}"; do
      if ! kill -0 "$pid" 2>/dev/null; then
        wait "$pid"
        rc=$?
        used=$(( used - pmem[$pid] ))
        if [ $rc -eq 0 ]; then
          if grep -q "pas de recompilation" "$BUILD_LOGS/${pname[$pid]}.log"; then
            echo "  ${pname[$pid]} : inchange"
          else
            echo "  ${pname[$pid]} : compile ($(( SECONDS - pstart[$pid] )) s)"
          fi
        else
          echo "  ${pname[$pid]} : ECHEC (journal $BUILD_LOGS/${pname[$pid]}.log)"
          tail -n 15 "$BUILD_LOGS/${pname[$pid]}.log"
          fails=$(( fails + 1 ))
        fi
        unset "pmem[$pid]" "pname[$pid]" "pstart[$pid]"
      fi
    done
  done
  PJ_NAMES=()
  PJ_MEM=()
  PJ_FUNCS=()
  return $fails
}

# ------------------------------------------------------------------------------
# Programmes

# Programme simple : make dans src/<nom>, binaire <nom> copie dans bin
bj_simple()   # nom [binaire]
{
  local name=$1 bin=${2:-$1} s
  s=$(src_sum "$S/$name")
  build_skip "$name" "$s" "$B/$bin" && return 0
  cd "$S/$name" && make && bj_put "$bin" "$B" || return 1
  build_remember "$name" "$s" "$B/$bin"
}

job_gui()
{
  local s
  s=$(src_sum "$S/gui")
  build_skip gui "$s" "$B/rpidatvgui" && return 0
  cd "$S/gui" && make && bj_put rpidatvgui "$B" || return 1
  build_remember gui "$s" "$B/rpidatvgui"
}

job_siggen()        { bj_simple siggen; }
job_adf4351()       { bj_simple adf4351; }
job_airspyview()    { bj_simple airspyview; }
job_rtlsdrview()    { bj_simple rtlsdrview; }
job_plutoview()     { bj_simple plutoview; }
job_power_meter()   { bj_simple power_meter; }
job_nf_meter()      { bj_simple nf_meter; }
job_pluto_nf_meter() { bj_simple pluto_nf_meter; }
job_sweeper()       { bj_simple sweeper; }
job_dmm()           { bj_simple dmm; }
job_noise_meter()   { bj_simple noise_meter; }
job_pluto_noise_meter() { bj_simple pluto_noise_meter; }
job_rydemon()       { bj_simple rydemon; }
job_ook48()         { bj_simple ook48; }
job_fake_read()     { bj_simple fake_read; }
job_keyedtx()       { bj_simple keyedtx; }
job_keyedtxtouch()  { bj_simple keyedtxtouch; }
job_atten()         { bj_simple atten set_attenuator; }

job_bandview()
{
  bj_simple bandview || return 1
  # Fichier fftw : pas de delai au demarrage de BandViewer et du NF Meter
  cp "$S/bandview/.fftwf_wisdom" "$BJ_HOME/.fftwf_wisdom"
}

job_wav2lime()
{
  local s
  s=$(src_sum "$S/wav2lime")
  build_skip wav2lime "$s" "$B/wav2lime" && return 0
  cd "$S/wav2lime" && gcc -o wav2lime wav2lime.c -lLimeSuite && bj_put wav2lime "$B" || return 1
  build_remember wav2lime "$s" "$B/wav2lime"
}

job_cam_ctl()
{
  local s
  s=$(src_sum "$S/cam_ctl")
  build_skip cam_ctl "$s" "$B/cam_ctl" && return 0
  cd "$S/cam_ctl" && rm -f cam_ctl && gcc ./cam_ctl.c -lm -lcurl -o ./cam_ctl && bj_put cam_ctl "$B" || return 1
  build_remember cam_ctl "$s" "$B/cam_ctl"
}

job_serial_com()
{
  local s
  s=$(src_sum "$S/serial_com")
  build_skip serial_com "$s" "$B/serial_com" && return 0
  cd "$S/serial_com" && rm -f serial_com \
    && gcc serial_com.c -o serial_com -I/usr/include/libusb-1.0 -L/usr/lib/arm-linux-gnueabihf -lusb-1.0 \
    && bj_put serial_com "$B" || return 1
  build_remember serial_com "$s" "$B/serial_com"
}

job_muntjac()
{
  local s
  s=$(src_sum "$S/muntjac")
  build_skip muntjac "$s" "$B/muntjacsdr_dvb" && return 0
  cd "$S/muntjac" && gcc muntjacsdr_dvb.c dvbs2neon.S -mfpu=neon -lpthread -o muntjacsdr_dvb \
    && bj_put muntjacsdr_dvb "$B" || return 1
  build_remember muntjac "$s" "$B/muntjacsdr_dvb"
}

job_dvb_t_stack()
{
  local s
  s=$(src_sum "$S/dvb_t_stack")
  # Micrologiciel du DATV Express
  sys cp "$S/dvb_t_stack/datvexpress16.ihx" /lib/firmware/datvexpress/datvexpress16.ihx
  sys cp "$S/dvb_t_stack/datvexpressraw16.rbf" /lib/firmware/datvexpress/datvexpressraw16.rbf
  build_skip dvb_t_stack "$s" "$B/dvb_t_stack" && return 0
  cd "$S/dvb_t_stack/Release" && make clean && make && bj_put dvb_t_stack "$B" || return 1
  build_remember dvb_t_stack "$s" "$B/dvb_t_stack"
}

job_rx_tools()
{
  local s
  s=$(src_sum "$S/rx_tools/src" "$S/rx_tools/CMakeLists.txt")
  build_skip rx_tools "$s" /usr/local/bin/rx_fm /usr/local/bin/rx_power /usr/local/bin/rx_sdr && return 0
  cd "$S/rx_tools" && cmake . && make || return 1
  sys make install || return 1
  [ "$BJ_SYS" == "1" ] && build_remember rx_tools "$s" /usr/local/bin/rx_fm /usr/local/bin/rx_power /usr/local/bin/rx_sdr
  return 0
}

job_sstv()
{
  local s
  s=$(src_sum "$S/SSTV")
  build_skip sstv "$s" "$B/sstv_decode" "$B/sstv_gui" "$B/sstv_radio" && return 0
  cd "$S/SSTV" && ./scripts/build.sh && bj_put bin/sstv_decode "$B" && bj_put bin/sstv_gui "$B" \
    && bj_put bin/sstv_radio "$B" || return 1
  build_remember sstv "$s" "$B/sstv_decode" "$B/sstv_gui" "$B/sstv_radio"
}

# Decodeur multiple : reprend le dessin de la GUI (Graphics.h, button3d.h,
# polices, ffunc), d'ou ces fichiers dans l'empreinte
job_multidec()
{
  local s
  s=$(src_sum "$S/multidec" "$S/gui/Graphics.h" "$S/gui/button3d.h" "$S/gui/ffunc.c" "$S/gui/ffunc.h" "$S/gui/font")
  build_skip multidec "$s" "$B/multidec" "$B/md_level" "$B/md_ft8" "$B/md_audio" "$B/md_fmdemod" && return 0
  cd "$S/multidec" && make && bj_put multidec "$B" && bj_put md_level "$B" && bj_put md_ft8 "$B" \
    && bj_put md_audio "$B" && bj_put md_fmdemod "$B" || return 1
  build_remember multidec "$s" "$B/multidec" "$B/md_level" "$B/md_ft8" "$B/md_audio" "$B/md_fmdemod"
}

job_longmynd()
{
  local s
  s=$(src_sum "$S/longmynd")
  if ! build_skip longmynd "$s" "$BJ_HOME/longmynd/longmynd"; then
    rm -rf "$BJ_HOME/longmynd"
    cp -r "$S/longmynd/" "$BJ_HOME/" && cd "$BJ_HOME/longmynd" && make || return 1
    build_remember longmynd "$s" "$BJ_HOME/longmynd/longmynd"
  fi
  # Regles udev du MiniTiouner (et du PicoTuner)
  if ! grep -q "ba2c" /etc/udev/rules.d/minitiouner.rules 2>/dev/null; then
    sys cp "$BJ_HOME/longmynd/minitiouner.rules" /etc/udev/rules.d/
  fi
  return 0
}

# Modulateur DVB (libdvbmod, telecharge) et LimeSDR Toolbox
job_limesdr_toolbox()
{
  local s bins
  cd "$S/limesdr_toolbox" || return 1
  wget -q https://github.com/F5OEO/libdvbmod/archive/master.zip -O libdvbmod.zip || return 1
  rm -rf libdvbmod libdvbmod-master
  unzip -q -o libdvbmod.zip && mv libdvbmod-master libdvbmod && rm libdvbmod.zip || return 1
  bins="$B/dvb2iq $B/limesdr_send $B/limesdr_dump $B/limesdr_stopchannel $B/limesdr_forward $B/limesdr_dvb"
  s=$(src_sum "$S/limesdr_toolbox")
  build_skip limesdr_toolbox "$s" $bins && return 0
  cd libdvbmod/libdvbmod && make && cd ../DvbTsToIQ && make && bj_put dvb2iq "$B" || return 1
  cd "$S/limesdr_toolbox" && make || return 1
  bj_put limesdr_send "$B" && bj_put limesdr_dump "$B" && bj_put limesdr_stopchannel "$B" \
    && bj_put limesdr_forward "$B" || return 1
  make dvb && bj_put limesdr_dvb "$B" || return 1
  build_remember limesdr_toolbox "$s" $bins
}

# Recepteur LeanDVB (depot f4dvk/leansdr, telecharge)
job_leandvb()
{
  local s
  cd "$S" || return 1
  wget -q https://github.com/f4dvk/leansdr/archive/master.zip -O leansdr.zip || return 1
  rm -rf leansdr leansdr-master
  unzip -q leansdr.zip && mv leansdr-master leansdr && rm leansdr.zip || return 1
  s=$(src_sum "$S/leansdr/src")
  build_skip leandvb "$s" "$B/leandvb" && return 0
  cd leansdr/src/apps && make native && bj_put leandvb "$B" || return 1
  build_remember leandvb "$s" "$B/leandvb"
}

# Codeur H264 : avc2ts.cpp de la branche portsdown4 du BATC (les autres
# sources et bibliotheques sont dans $BJ_HOME/avc2ts depuis l'installation)
job_avc2ts()
{
  cd "$BJ_HOME/avc2ts" || return 1
  wget -q https://github.com/BritishAmateurTelevisionClub/avc2ts/raw/portsdown4/avc2ts.cpp -O avc2ts.cpp.new || return 1
  # Fichier identique : on garde l'ancien (et sa date), make ne recompile rien
  if cmp -s avc2ts.cpp.new avc2ts.cpp; then
    rm avc2ts.cpp.new
  else
    mv -f avc2ts.cpp.new avc2ts.cpp
  fi
  make && bj_put avc2ts "$B"
}

# Client du HamTV Merger (depot ARISS-UK, telecharge)
job_tsmerge()
{
  local s t=$BJ_HOME/tsmerge_new
  rm -rf "$t" && mkdir -p "$t" && cd "$t" || return 1
  wget -q https://github.com/ARISS-UK/tsmerge-client-linuxcli/archive/refs/heads/master.zip -O tsmerge.zip \
    && unzip -q tsmerge.zip || return 1
  s=$(src_sum "$t/tsmerge-client-linuxcli-master")
  if build_skip tsmerge "$s" "$BJ_HOME/tsmerge/tsmerge-client-linuxcli"; then
    rm -rf "$t"
    return 0
  fi
  rm -rf "$BJ_HOME/tsmerge"
  mv "$t/tsmerge-client-linuxcli-master" "$BJ_HOME/tsmerge" && rm -rf "$t" || return 1
  cd "$BJ_HOME/tsmerge" && make cppcheck && make || return 1
  build_remember tsmerge "$s" "$BJ_HOME/tsmerge/tsmerge-client-linuxcli"
}

# MeteorViewer et SDRplay Band Viewer : il faut l'API SDRplay, sinon elle est
# installee au prochain demarrage (.post-install_actions)
job_sdrplay()
{
  local s
  if [ ! -f /usr/local/include/sdrplay_api.h ]; then
    cd "$S/meteorview" || return 1
    if [ ! -f SDRplay_RSP_API-ARM-3.09.1.run ]; then
      wget -q https://www.sdrplay.com/software/SDRplay_RSP_API-ARM-3.09.1.run
    fi
    chmod +x SDRplay_RSP_API-ARM-3.09.1.run
    touch "$BJ_HOME/rpidatv/.post-install_actions"
    return 0
  fi
  s=$(src_sum "$S/meteorview")
  if ! build_skip meteorview "$s" "$B/meteorview"; then
    if cd "$S/meteorview" && make && bj_put meteorview "$B"; then
      build_remember meteorview "$s" "$B/meteorview"
    else
      touch "$BJ_HOME/rpidatv/.post-install_actions"
      return 0
    fi
  fi
  s=$(src_sum "$S/sdrplayview")
  if ! build_skip sdrplayview "$s" "$B/sdrplayview"; then
    if cd "$S/sdrplayview" && make && bj_put sdrplayview "$B"; then
      build_remember sdrplayview "$s" "$B/sdrplayview"
    else
      touch "$BJ_HOME/rpidatv/.post-install_actions"
    fi
  fi
  return 0
}

# Tous les programmes Portsdown, les plus gourmands en tete de file.
# rtl-sdr et LimeSuite doivent etre installes avant (rx_tools, rtlsdrview).
build_portsdown_programs()
{
  if [ "$PORTSDOWN_FULL_BUILD" == "1" ]; then
    rm -rf "$BUILD_SUMS"
  fi
  rm -rf "$BUILD_LOGS"
  pj_add leandvb          175 job_leandvb
  pj_add avc2ts           135 job_avc2ts
  pj_add sstv             95  job_sstv
  pj_add multidec         50  job_multidec
  pj_add gui              90  job_gui
  pj_add sdrplay          60  job_sdrplay
  pj_add airspyview       55  job_airspyview
  pj_add siggen           30  job_siggen
  pj_add dvb_t_stack      35  job_dvb_t_stack
  pj_add limesdr_toolbox  35  job_limesdr_toolbox
  pj_add longmynd         30  job_longmynd
  pj_add rx_tools         30  job_rx_tools
  pj_add bandview         25  job_bandview
  pj_add rtlsdrview       25  job_rtlsdrview
  pj_add plutoview        25  job_plutoview
  pj_add power_meter      25  job_power_meter
  pj_add nf_meter         25  job_nf_meter
  pj_add pluto_nf_meter   25  job_pluto_nf_meter
  pj_add sweeper          25  job_sweeper
  pj_add dmm              25  job_dmm
  pj_add noise_meter      25  job_noise_meter
  pj_add pluto_noise_meter 25 job_pluto_noise_meter
  pj_add tsmerge          25  job_tsmerge
  pj_add muntjac          20  job_muntjac
  pj_add rydemon          20  job_rydemon
  pj_add ook48            20  job_ook48
  pj_add fake_read        20  job_fake_read
  pj_add adf4351          20  job_adf4351
  pj_add keyedtx          20  job_keyedtx
  pj_add keyedtxtouch     20  job_keyedtxtouch
  pj_add atten            20  job_atten
  pj_add wav2lime         20  job_wav2lime
  pj_add cam_ctl          20  job_cam_ctl
  pj_add serial_com       20  job_serial_com
  pj_run
}
