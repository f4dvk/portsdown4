#!/bin/bash
# run_decoder.sh : chaine de reception d'un decodeur du decodeur multiple.
#
# Lance par multidec dans son propre groupe de processus (arret : tout le
# groupe d'un coup). Ce que ce script ecrit sur stdout est lu par multidec ;
# stderr va dans /tmp/multidec_<decodeur>.log.
#
# Variables fournies par multidec :
#   MD_DEC   decodeur (pocsag, aprs, rtty...)      MD_FREQ  frequence (MHz)
#   MD_GAIN  gain (dB) ou "auto"                   MD_PPM   correction (ppm)
#   MD_OPT_<cle>  options du decodeur              MD_HF    v4 ou direct (HF)
#   MD_LAT, MD_LON, MD_CALL, MD_LOC  station       MD_SHM   dossier en RAM
# Le son part vers md_audio : haut-parleur du Pi et page web /touchscreen/
# Mode balayage des radiosondes : MD_SCAN=1 (ecrit "SCAN <MHz>" puis s'arrete)

B=/home/pi/rpidatv/bin
S=${MD_SHM:-/dev/shm/multidec}
mkdir -p "$S"
F=${MD_FREQ:-144.8}
PPM=${MD_PPM:-0}
GAIN=${MD_GAIN:-auto}
LB="stdbuf -oL"                       # sortie ligne par ligne vers multidec

# Options rtl_fm : gain (automatique si absent), correction, HF en
# echantillonnage direct (cles sans convertisseur HF ; la RTL-SDR Blog V4
# recoit le HF sans option)
RTLFM="-p $PPM"
if [ "$GAIN" != "auto" ]; then RTLFM="$RTLFM -g $GAIN"; fi
if [ "$MD_HF" == "direct" ] && [ "$(echo "$F < 24" | bc)" == "1" ]; then RTLFM="$RTLFM -E direct2"; fi

# Audio FM bande etroite a 22050 Hz pour multimon-ng
fm22()
{
  rtl_fm -M fm -f "${F}M" -s 22050 $RTLFM - | $B/md_level -r 22050 -m fm
}

case "$MD_DEC" in

pocsag)
  case "$MD_OPT_rate" in
    512|1200|2400) DEM="-a POCSAG$MD_OPT_rate" ;;
    *) DEM="-a POCSAG512 -a POCSAG1200 -a POCSAG2400" ;;
  esac
  TYPE=""
  if [ "$MD_OPT_type" == "alpha" ] || [ "$MD_OPT_type" == "numeric" ]; then TYPE="-f $MD_OPT_type"; fi
  fm22 | multimon-ng -t raw -c $DEM -e --json -C "${MD_OPT_charset:-US}" $TYPE -
  ;;

flex)
  fm22 | multimon-ng -t raw -c -a "${MD_OPT_variant:-FLEX_NEXT}" --json -
  ;;

selcall)
  # Une seule famille de tonalites a la fois avec le DTMF : leurs jeux de
  # frequences se recouvrent (essai : ZVEI1 "12345" lu aussi en ZVEI2/3 et CCIR)
  case "$MD_OPT_modes" in
    zvei2) DEM="-a DTMF -a ZVEI2" ;;
    zvei3) DEM="-a DTMF -a ZVEI3" ;;
    ccir) DEM="-a DTMF -a CCIR" ;;
    eea) DEM="-a DTMF -a EEA" ;;
    *) DEM="-a DTMF -a ZVEI1" ;;
  esac
  fm22 | multimon-ng -t raw -c $DEM --json -
  ;;

cw)
  # BLU : la porteuse est decalee de la tonalite choisie (700 Hz par defaut)
  TONE=${MD_OPT_tone:-700}
  FT=$(echo "scale=6; $F - $TONE / 1000000" | bc)
  rtl_fm -M usb -f "${FT}M" -s 22050 $RTLFM - | $B/md_level -r 22050 | multimon-ng -t raw -c -a MORSE_CW -
  ;;

aprs)
  # direwolf : journal CSV (positions decodees, Mic-E compris) dans un tube
  RATE=24000
  MODEM=1200
  if [ "$MD_OPT_baud" == "9600" ]; then RATE=48000; MODEM=9600; fi
  printf "ADEVICE stdin null\nARATE %s\nACHANNELS 1\nCHANNEL 0\nMYCALL N0CALL\nMODEM %s\n" \
    $RATE $MODEM > "$S/direwolf.conf"
  rm -f "$S/aprs.csv"
  mkfifo "$S/aprs.csv"
  cat "$S/aprs.csv" &
  rtl_fm -M fm -f "${F}M" -s $RATE $RTLFM - | $B/md_level -r $RATE -m fm \
    | direwolf -c "$S/direwolf.conf" -r $RATE -B $MODEM -D 1 -t 0 -q hd -L "$S/aprs.csv" - > /dev/null
  ;;

rtty)
  # BLU, puis minimodem ; mark 1700 Hz, space 1700 - shift. Seuil de
  # confiance 3 (bruit seul : ~1,6 ; signal d'essai : 16 a 27). Detection
  # automatique de la porteuse (-a) en Baudot seulement : en ASCII elle
  # empeche tout decodage (essai : signal decale de 120 Hz decode sans -a)
  SHIFT=${MD_OPT_shift:-425}
  case "$MD_OPT_code" in
    baudot) CODE="--baudot -a" ;;
    ascii8) CODE="--ascii" ;;
    *) CODE="-7" ;;
  esac
  MODE=usb
  if [ "$MD_OPT_demod" == "lsb" ]; then MODE=lsb; fi
  rtl_fm -M $MODE -f "${F}M" -s 12000 $RTLFM - | $B/md_level -r 12000 \
    | sox -t raw -r 12000 -e signed -b 16 -c 1 - -t wav - 2>/dev/null \
    | minimodem --rx -f - -c 3 -M 1700 -S $((1700 - SHIFT)) $CODE --stopbits 1 "${MD_OPT_baud:-50}" 2>&1
  ;;

sonde)
  if [ "$MD_SCAN" == "1" ]; then
    # Balayage 400-406 MHz (comme radiosonde_auto_rx) : les 5 raies les plus
    # fortes, au moins 8 dB au-dessus du bruit et a plus de 20 kHz l'une de
    # l'autre, sont ecoutees tour a tour ; dft_detect reconnait le type de
    # sonde. Essai : une porteuse fixe a 403,2 MHz, qui n'est pas une sonde,
    # etait la raie la plus forte.
    G=""
    if [ "$GAIN" != "auto" ]; then G="-g $GAIN"; fi
    while true; do
      rtl_power -f 400M:406M:2k -i 2 -1 -p $PPM $G 2>/dev/null \
        | awk -F', ' '{ for (i = 7; i <= NF; i++) printf "%.0f %s\n", $3 + (i - 7) * $5, $i }' \
        | sort -k2 -g > "$S/scan.txt"
      N=$(wc -l < "$S/scan.txt")
      if [ "$N" -gt 100 ]; then
        MED=$(sed -n "$((N / 2))p" "$S/scan.txt" | cut -d' ' -f2)
        PEAKS=$(sort -k2 -g -r "$S/scan.txt" | awk -v m="$MED" '$2 - m >= 8 {
            ok = 1; for (k in f) if ($1 - f[k] < 20000 && f[k] - $1 < 20000) ok = 0
            if (ok) { f[n++] = $1; printf "%.3f:%.1f\n", $1 / 1e6, $2 - m } if (n == 5) exit }')
        for P in $PEAKS; do
          FQ=${P%%:*}
          echo "SCANNING $FQ"
          T=$(timeout 9 rtl_fm -M raw -F 9 -f "${FQ}M" -s 48000 $RTLFM - 2>/dev/null \
                | timeout 8 dft_detect -t 6 --IQ 0.0 --dc - 48000 16 2>/dev/null | head -1)
          if [ -n "$T" ]; then
            echo "SCAN $FQ ${P##*:} ${T%%:*}"
            exit 0
          fi
        done
      fi
      echo "SCANNING"
    done
  fi
  # Une seule capture IQ a 48 kHz, envoyee a chaque demodulateur de rs1729
  OPT="--IQ 0.0 --lpIQ --dc --json"
  RAW="- 48000 16"
  J="grep --line-buffered ^{"
  case "$MD_OPT_types" in
    rs41)   DECS="rs41" ;;
    m10m20) DECS="m10m20" ;;
    dfm)    DECS="dfm" ;;
    other)  DECS="rs92 lms6 meisei imet54 mp3h1 mts01" ;;
    *)      DECS="rs41 m10m20 dfm" ;;
  esac
  CMDS=()
  for t in $DECS; do
    case $t in
      rs41)   CMDS+=("rs41mod $OPT --ptu2 --ecc2 --crc $RAW") ;;
      # -vv : humidite des M10 (approchee : affichee ~), seule difference du JSON
      m10m20) CMDS+=("m10m20mod $OPT --ptu -vv $RAW") ;;
      dfm)    CMDS+=("dfm09mod $OPT --ecc --ptu $RAW") ;;
      rs92)   CMDS+=("rs92mod $OPT --ecc --crc $RAW") ;;
      lms6)   CMDS+=("lms6Xmod $OPT --vit --ecc $RAW") ;;
      meisei) CMDS+=("meisei100mod $OPT $RAW") ;;
      imet54) CMDS+=("imet54mod $OPT $RAW") ;;
      mp3h1)  CMDS+=("mp3h1mod $OPT $RAW") ;;
      mts01)  CMDS+=("mts01mod $OPT $RAW") ;;
    esac
  done
  # Chaque demodulateur dans une branche de tee, sa sortie sur le descripteur
  # 3 (stdout de ce script, vers multidec) : sans cela, la sortie d'une branche
  # >(...) part dans l'etape suivante du tube (essai : les trames M10 et DFM
  # du mode "All" finissaient dans l'entree IQ de rs41mod, perdues)
  exec 3>&1
  TEE=""
  for c in "${CMDS[@]}"; do TEE="$TEE >($c 2>/dev/null | $J >&3)"; done
  # ecoute (option) : demodulation FM avec squelch, filtre du son selon le type
  # de sonde (largeur de depart ; multidec la corrige au type decode)
  SQ=1
  if [ "$MD_OPT_squelch" == "off" ]; then SQ=0; fi
  case "$MD_OPT_types" in rs41) BW=10 ;; m10m20) BW=24 ;; dfm) BW=12 ;; *) BW=16 ;; esac
  if [ "$MD_OPT_audio" != "off" ]; then
    TEE="$TEE >($B/md_fmdemod -r 48000 -s $SQ -b $BW | $B/md_audio -r 48000 -c 1)"
  fi
  # -F 9 : vrai filtre a la decimation de rtl_fm (sinon simple moyenne, lobes a -13 dB)
  eval "rtl_fm -M raw -F 9 -f ${F}M -s 48000 $RTLFM - | $B/md_level -r 48000 -c 2 | tee $TEE > /dev/null"
  ;;

adsb)
  mkdir -p "$S/adsb"
  G=""
  if [ "$GAIN" != "auto" ]; then G="--gain=$GAIN"; fi
  NET=""
  if [ "$MD_OPT_net" == "on" ]; then NET="--net"; fi
  POS=""
  if [ -n "$MD_LAT" ]; then POS="--lat=$MD_LAT --lon=$MD_LON"; fi
  exec readsb --device-type rtlsdr --ppm=$PPM $G --write-json="$S/adsb" --write-json-every=1 $POS $NET --quiet
  ;;

ism)
  FOPT="-f ${F}M"
  if [ "$MD_OPT_hop" == "on" ]; then FOPT="-f 433.92M -f 868.3M -H 30"; fi
  if [ "$F" == "868.950" ] && [ "$MD_OPT_hop" != "on" ]; then FOPT="$FOPT -s 1600k"; fi   # wM-Bus
  G=""
  if [ "$GAIN" != "auto" ]; then G="-g $GAIN"; fi
  exec $LB rtl_433 $FOPT -p $PPM $G -F json -M level -M time:iso
  ;;

remotes)
  # Telecommandes : portails, garages, volets, cles de voiture, sonnettes.
  # Protocoles de rtl_433 25.12 (numeros de cette version, voir rtl_433 -R help),
  # codes tournants compris (KeeLoq, Security+, Somfy...), et en decodeurs
  # "flex" les codes fixes que lit le Flipper Zero, avec les durees de son
  # firmware : CAME (320/640 us), Nice FLO (700/1400), Holtek HT12 (430/870) ;
  # Princeton / EV1527 est natif (30). invert=1 : meme sens de bits que le
  # Flipper (impulsion longue = 1). Reception seulement.
  R="-R 1 -R 4 -R 14 -R 15 -R 24 -R 30 -R 46 -R 48 -R 51 -R 61 -R 62 -R 64 -R 93 -R 101 -R 115 -R 116 -R 128"
  R="$R -R 131 -R 164 -R 167 -R 169 -R 178 -R 187 -R 189 -R 191 -R 199 -R 207 -R 211 -R 218 -R 227 -R 286"
  if [ "$MD_OPT_set" == "alarm" ]; then      # + detecteurs d'alarme (ouverture, mouvement, claviers)
    R="$R -R 23 -R 29 -R 68 -R 70 -R 87 -R 99 -R 100 -R 102 -R 148 -R 192 -R 204 -R 209 -R 210 -R 258 -R 266"
  fi
  X="-X n=CAME,m=OOK_PWM,s=320,l=640,r=2500,t=100,bits>=12,bits<=25,invert=1"
  X="$X -X n=Nice-FLO,m=OOK_PWM,s=700,l=1400,r=5000,t=200,bits>=12,bits<=13,invert=1"
  X="$X -X n=Holtek-HT12,m=OOK_PWM,s=430,l=870,r=3000,t=100,bits>=12,bits<=13,invert=1"
  G=""
  if [ "$GAIN" != "auto" ]; then G="-g $GAIN"; fi
  exec $LB rtl_433 -f ${F}M -p $PPM $G $R $X -F json -M level -M time:iso
  ;;

ais)
  G=""
  if [ "$GAIN" != "auto" ]; then G="-gr TUNER $GAIN"; fi
  exec $LB AIS-catcher -d:0 -p $PPM $G -o 5
  ;;

acars)
  CH="131.525 131.725 131.825"
  if [ "$MD_OPT_chans" == "eu2" ]; then CH="131.450 131.475 131.525 131.550 131.725 131.825 131.850"; fi
  G=""
  if [ "$GAIN" != "auto" ]; then G="-g $GAIN"; fi
  exec $LB acarsdec --output json:file:path=- -e $G -p $PPM --rtlsdr 0 $CH
  ;;

vdl2)
  G=""
  if [ "$GAIN" != "auto" ]; then G="--gain $GAIN"; fi
  exec $LB dumpvdl2 --output decoded:json:file:path=- --rtlsdr 0 $G --correction $PPM \
    136725000 136775000 136875000 136975000
  ;;

dmr)
  # dsd-fme pilote la cle ; la voix decodee part en UDP vers aplay
  case "$MD_OPT_mode" in
    dmr) M="-fs" ;; dpmr) M="-fm" ;; nxdn48) M="-fi" ;; dstar) M="-fd" ;;
    ysf) M="-fy" ;; m17) M="-fz" ;; nxdn96) M="-fn" ;; p25) M="-f1" ;; *) M="-fa" ;;
  esac
  G=0
  if [ "$GAIN" != "auto" ]; then G=$GAIN; fi
  OUT=null
  if [ "$MD_OPT_audio" != "off" ]; then
    OUT=udp:127.0.0.1:23456
    socat -u UDP-RECV:23456 STDOUT 2>/dev/null | $B/md_audio -r 8000 -c 1 &
  fi
  $LB dsd-fme -i "rtl:0:${F}M:$G:$PPM:12:0:2" $M -o $OUT 2>&1
  ;;

ft8)
  M=""
  if [ "$MD_OPT_mode" == "ft4" ]; then M="-4"; fi
  rtl_fm -M usb -f "${F}M" -s 12000 $RTLFM - | $B/md_ft8 -r 12000 $M
  ;;

wspr)
  G="-a"
  if [ "$GAIN" != "auto" ]; then G="-g $GAIN"; fi
  D=""
  if [ "$MD_HF" == "direct" ] && [ "$(echo "$F < 24" | bc)" == "1" ]; then D="-d 2"; fi
  X="-x"
  if [ "$MD_OPT_upload" == "on" ]; then X=""; fi
  exec $LB rtlsdr_wsprd -f "${F}M" -c "${MD_CALL:-N0CALL}" -l "${MD_LOC:-JN00}" -p $PPM $G $D $X
  ;;

rds)
  # Signal FM complet (MPX) a 171 kHz pour redsea ; ecoute (option) : audio
  # mono passe-bas 15 kHz, desaccentue 50 us (passe-bas a 1 pole, 3183 Hz)
  D=$(dirname "$0")
  if [ "$MD_SCAN" == "1" ]; then exec python3 "$D/md_radio.py" fm-scan; fi
  MPX="rtl_fm -M fm -l 0 -A std -s 171k -F 9 -f ${F}M $RTLFM -"
  if [ "$MD_OPT_audio" == "on" ]; then
    $MPX | $B/md_level -r 171000 -m fm \
      | tee >(sox -t raw -r 171000 -e signed -b 16 -c 1 - -t raw -r 48000 -e signed -b 16 -c 1 - \
                lowpass 15000 lowpass -1 3183 2>/dev/null | $B/md_audio -r 48000 -c 1) \
      | $LB redsea -r 171k
  else
    $MPX | $B/md_level -r 171000 -m fm | $LB redsea -r 171k
  fi
  ;;

dab)
  # welle-cli pilote la cle et sert mux.json (et l'ecoute en MP3) sur le port
  # 7979 ; carrousel : un programme decode a la fois, le temps d'avoir son
  # texte (DLS). md_radio.py en tire les changements pour multidec, et joue
  # sur le Pi le service touche a l'ecran (dab_play : ffmpeg -> md_audio).
  D=$(dirname "$0")
  if [ "$MD_SCAN" == "1" ]; then exec python3 "$D/md_radio.py" dab-scan; fi
  CH=${MD_CHAN:-8D}
  welle-cli -c "$CH" -C 1 -P -T -g "$(python3 "$D/md_radio.py" gain-index "$GAIN")" -w 7979 \
    > /dev/null 2> "$S/welle.log" &
  exec python3 "$D/md_radio.py" dab-poll 7979 "$CH"
  ;;

*)
  echo "Decodeur inconnu : $MD_DEC" >&2
  exit 1
  ;;
esac
