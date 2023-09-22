#!/bin/bash

# Amended 201807090 DGC

# Set the look-up files
PATHBIN="/home/pi/rpidatv/bin/"
PATHSCRIPT="/home/pi/rpidatv/scripts"
PCONFIGFILE="/home/pi/rpidatv/scripts/portsdown_config.txt"
RCONFIGFILE="/home/pi/rpidatv/scripts/longmynd_config.txt"
RXPRESETSFILE="/home/pi/rpidatv/scripts/rx_presets.txt"
RTLPRESETSFILE="/home/pi/rpidatv/scripts/rtl-fm_presets.txt"

# Define proc to look-up with
get_config_var() {
lua - "$1" "$2" <<EOF
local key=assert(arg[1])
local fn=assert(arg[2])
local file=assert(io.open(fn))
for line in file:lines() do
local val = line:match("^#?%s*"..key.."=(.*)$")
if (val ~= nil) then
print(val)
break
end
end
EOF
}

# Look up and calculate the Receive parameters

SYMBOLRATEK=$(get_config_var rx0sr $RXPRESETSFILE)
let SYMBOLRATE=SYMBOLRATEK*1000

#UPSAMPLE=$(get_config_var upsample $PCONFIGFILE)
UPSAMPLE=4
UPSAMPLE_RX=$(get_config_var upsample $RXPRESETSFILE)

MODE_OUTPUT=$(get_config_var modeoutput $PCONFIGFILE)

FREQ_TX=$(get_config_var freqoutput $PCONFIGFILE)
LIME_TX_GAIN=$(get_config_var limegain $PCONFIGFILE)
BAND_GPIO=$(get_config_var expports $PCONFIGFILE)
MODULATION_TX=$(get_config_var modulation $PCONFIGFILE)
FEC_TX=$(get_config_var fec $PCONFIGFILE)

ETAT=$(get_config_var etat $RXPRESETSFILE)

Lock="0"
old="0"
modcod=0
top=0

case "$MODULATION_TX" in
  "S2QPSK" )
    case "$FEC_TX" in
      "14" ) modcod=1 ;;
      "13" ) modcod=2 ;;
      "25" ) modcod=3 ;;
      "12" ) modcod=4 ;;
      "35" ) modcod=5 ;;
      "23" ) modcod=6 ;;
      "34" ) modcod=7 ;;
      "45" ) modcod=8 ;;
      "56" ) modcod=9 ;;
      "89" ) modcod=10 ;;
      "91" ) modcod=11 ;;
    esac ;;
  "8PSK" )
    case "$FEC_TX" in
      "35" ) modcod=12 ;;
      "23" ) modcod=13 ;;
      "34" ) modcod=14 ;;
      "56" ) modcod=15 ;;
      "89" ) modcod=16 ;;
      "91" ) modcod=17 ;;
    esac ;;
  "16APSK" )
    case "$FEC_TX" in
      "23" ) modcod=18 ;;
      "34" ) modcod=19 ;;
      "45" ) modcod=20 ;;
      "56" ) modcod=21 ;;
      "89" ) modcod=22 ;;
      "91" ) modcod=23 ;;
    esac ;;
  "32APSK" )
    case "$FEC_TX" in
      "34" ) modcod=24 ;;
      "45" ) modcod=25 ;;
      "56" ) modcod=26 ;;
      "89" ) modcod=27 ;;
      "91" ) modcod=28 ;;
    esac ;;
esac

# Allow for GPIOs in 16 - 31 range (direct setting)
if [ "$BAND_GPIO" -gt "15" ]; then
  let BAND_GPIO=$BAND_GPIO-16
fi

LIME_TX_GAINA=`echo - | awk '{print '$LIME_TX_GAIN' / 100}'`

if [ "$LIME_TX_GAIN" -lt 6 ]; then
  LIME_TX_GAINA=`echo - | awk '{print ( '$LIME_TX_GAIN' - 6 ) / 100}'`
fi

let DIGITAL_GAIN=($LIME_TX_GAIN*31)/100 # For LIMEDVB

LIMETYPE=""

if [ "$MODE_OUTPUT" == "LIMEUSB" ]; then
  LIMETYPE="-U"
fi

FREQ_OUTPUT=$(get_config_var rx0frequency $RXPRESETSFILE)
FreqHz=$(echo "($FREQ_OUTPUT*1000000)/1" | bc )
#echo Freq = $FreqHz

MODULATION=$(get_config_var rx0modulation $RXPRESETSFILE)

if [ "$ETAT" != "ON" ]; then
  FEC=$(get_config_var rx0fec $RXPRESETSFILE)
  # Will need additional lines here to handle DVB-S2 FECs
  if [ "$FEC" != "Auto" ]; then
   let FECNUM=FEC
   let FECDEN=FEC+1
   FECDVB="--cr $FECNUM"/"$FECDEN"
  else
   FECDVB=""
  fi
else
  if [ "$MODULATION_TX" == "DVB-S" ]; then
    MODULATION="DVB-S"
    MODULATION_TX="DVBS"
    CONST="QPSK"
    modcod=""
    let FECNUM=FEC_TX
    let FECDEN=FEC_TX+1
    FECDVB="--fastlock --cr $FECNUM"/"$FECDEN"
  else
    MODULATION="DVB-S2"
    case "$MODULATION_TX" in
      "S2QPSK" )
        MODULATION_TX="DVBS2"
        CONST="QPSK"
      ;;
      "8PSK" )
        MODULATION_TX="DVBS2"
        CONST="8PSK"
      ;;
      "16APSK" )
        MODULATION_TX="DVBS2"
        CONST="16APSK"
      ;;
      "32APSK" )
        MODULATION_TX="DVBS2"
        CONST="32APSK"
      ;;
    esac
    modcod=$((2**$modcod))
    modcod="--modcods "$modcod
    FECDVB=""
  fi
  case "$FEC_TX" in
    "14" )
      FECNUM="1"
      FECDEN="4"
    ;;
    "13" )
      FECNUM="1"
      FECDEN="3"
    ;;
    "12" )
      FECNUM="1"
      FECDEN="2"
    ;;
    "35" )
      FECNUM="3"
      FECDEN="5"
    ;;
    "23" )
      FECNUM="2"
      FECDEN="3"
    ;;
    "34" )
      FECNUM="3"
      FECDEN="4"
    ;;
    "56" )
      FECNUM="5"
      FECDEN="6"
    ;;
    "89" )
      FECNUM="8"
      FECDEN="9"
    ;;
    "91" )
      FECNUM="9"
      FECDEN="10"
    ;;
  esac
fi

SDR=$(get_config_var rx0sdr $RXPRESETSFILE)

SAMPLERATEK=$(get_config_var rx0samplerate $RXPRESETSFILE)
if [ "$SAMPLERATEK" == "0" ]; then
  if [ "$SYMBOLRATEK" -lt 250 ]; then
    SR_RTLSDR=300000
  elif [ "$SYMBOLRATEK" -gt 249 ] && [ "$SYMBOLRATEK" -lt 500 ] && [ "$SDR" == "RTLSDR" ]; then
    SR_RTLSDR=1000000
  elif [ "$SYMBOLRATEK" -gt 499 ] && [ "$SYMBOLRATEK" -lt 1000 ]; then
    SR_RTLSDR=1200000
  elif [ "$SYMBOLRATEK" -gt 999 ] && [ "$SYMBOLRATEK" -lt 1101 ]; then
    SR_RTLSDR=1250000
  elif [ "$SYMBOLRATEK" -gt 249 ] && [ "$SYMBOLRATEK" -lt 500 ] && [ "$SDR" == "LIMEMINI" ]; then
    SR_RTLSDR=850000
  else
    SR_RTLSDR=2400000
  fi
else
  let SR_RTLSDR=SAMPLERATEK*1000
fi

#############################################

GAIN=$(get_config_var rx0gain $RXPRESETSFILE)

ENCODING=$(get_config_var rx0encoding $RXPRESETSFILE)

#SDR=$(get_config_var rx0sdr $RXPRESETSFILE)

GRAPHICS=$(get_config_var rx0graphics $RXPRESETSFILE)

PARAMS=$(get_config_var rx0parameters $RXPRESETSFILE)

SOUND=$(get_config_var rx0sound $RXPRESETSFILE)
AUDIO_OUT=$(get_config_var audio $RCONFIGFILE)
#if [ "$AUDIO_OUT" == "rpi" ]; then
#  AUDIO_MODE="local"
#else
#  AUDIO_MODE="alsa:plughw:1,0"
#fi

RPIJ_AUDIO_DEV="$(aplay -l 2> /dev/null | grep 'Headphones' | cut -c6-6)"

if [ "$RPIJ_AUDIO_DEV" == '' ]; then
  RPIJ_AUDIO_DEV="$(aplay -l 2> /dev/null | grep 'bcm2835 ALSA' | cut -c6-6)"
  if [ "$RPIJ_AUDIO_DEV" == '' ]; then
    printf "RPi Jack audio device was not found, setting to 0\n"
    RPIJ_AUDIO_DEV="0"
  fi
fi

# Take only the first character
RPIJ_AUDIO_DEV="$(echo $RPIJ_AUDIO_DEV | cut -c1-1)"

echo "The RPi Jack Audio Card number is -"$RPIJ_AUDIO_DEV"-"

###################################################################

USBOUT_AUDIO_DEV="$(aplay -l 2> /dev/null | grep 'USB Audio' | cut -c6-6)"

if [ "$USBOUT_AUDIO_DEV" == '' ]; then
  printf "USB Dongle audio device was not found, setting to RPi Jack\n"
  USBOUT_AUDIO_DEV=$RPIJ_AUDIO_DEV
fi

# Take only the first character
USBOUT_AUDIO_DEV="$(echo $USBOUT_AUDIO_DEV | cut -c1-1)"

echo "The USB Dongle Audio Card number is -"$USBOUT_AUDIO_DEV"-"

###################################################################

if [ "$AUDIO_OUT" == "rpi" ]; then
  AUDIO_OUT_DEV=$RPIJ_AUDIO_DEV
else
  AUDIO_OUT_DEV=$USBOUT_AUDIO_DEV
fi

FLOCK=$(get_config_var rx0fastlock $RXPRESETSFILE)
if [ "$FLOCK" == "ON" ]; then
  FASTLOCK="--fastlock"
else
  FASTLOCK=""
fi

if [ "$GAIN" -lt 10 ]; then
  GAIN_LIME="0.0$GAIN"
else
  GAIN_LIME="0.$GAIN"
fi

if [ "$GAIN" == 100 ] && [ "$SDR" == "LIMEMINI" ]; then
 GAIN_LIME=1
fi

MODE_STARTUP=$(get_config_var startup $PCONFIGFILE)

if [ "$MODE_STARTUP" == "Button_rx_boot" ]; then
  if [ "$FREQ_OUTPUT" == "145.9" ] && [ "$SDR" == "LIMEMINI" ]; then
    GAIN_LIME="0.8"
  elif [ "$FREQ_OUTPUT" == "437" ] && [ "$SDR" == "LIMEMINI" ]; then
    GAIN_LIME="0.7"
  elif [ "$FREQ_OUTPUT" == "1255" ] && [ "$SDR" == "LIMEMINI" ]; then
    GAIN_LIME="1"
  fi
fi

# Look up the RTL-SDR Frequency error from the RTL-FM file
FREQOFFSET=$(get_config_var roffset $RTLPRESETSFILE)

if [ "$SDR" == "RTLSDR" ]; then
  KEY="rtl_sdr -p $FREQOFFSET -g $GAIN -f $FreqHz -s $SR_RTLSDR - 2>/dev/null "
  B=""
fi
if [ "$SDR" == "LIMEMINI" ]; then
  KEY="/home/pi/rpidatv/bin/limesdr_dump -f $FreqHz -b 2.5e6 -s $SR_RTLSDR -r $UPSAMPLE_RX -g $GAIN_LIME -l 1024*1024 | buffer"
  B="--s12"
fi

# Clean up
sudo rm fifo.264 >/dev/null 2>/dev/null
sudo rm videots >/dev/null 2>/dev/null
sudo killall leandvb >/dev/null 2>/dev/null
sudo killall omxplayer.bin >/dev/null 2>/dev/null
mkfifo fifo.264
mkfifo videots

# Make sure that the screen background is all black
sudo killall fbi >/dev/null 2>/dev/null
sudo fbi -T 1 -noverbose -a $PATHSCRIPT"/images/Blank_Black.png"
(sleep 0.2; sudo killall -9 fbi >/dev/null 2>/dev/null) &  ## kill fbi once it has done its work

# Pipe the output from rtl-sdr to leandvb.  Then put videots in a fifo.

# Treat each display case differently


# Constellation and Parameters on
if [ "$GRAPHICS" == "ON" ] && [ "$PARAMS" == "ON" ] && [ "$ETAT" == "OFF" ]; then
  sudo $KEY\
    | $PATHBIN"leandvb" $B --fd-pp 3 --fd-info 2 --fd-const 2 $FECDVB $FASTLOCK --sr $SYMBOLRATE --standard $MODULATION --sampler rrc --rrc-steps 35 --rrc-rej 10 --roll-off 0.35 --ldpc-bf 100 -f $SR_RTLSDR >videots 3>fifo.iq &
fi

# Constellation on, Parameters off
if [ "$GRAPHICS" == "ON" ] && [ "$PARAMS" == "OFF" ] && [ "$ETAT" == "OFF" ]; then
  sudo $KEY\
    | $PATHBIN"leandvb" $B --fd-pp 3 --fd-const 2 $FECDVB $FASTLOCK --sr $SYMBOLRATE --standard $MODULATION --sampler rrc --rrc-steps 35 --rrc-rej 10 --roll-off 0.35 --ldpc-bf 100 -f $SR_RTLSDR >videots 3>fifo.iq &
fi

# Constellation off, Parameters on
if [ "$GRAPHICS" == "OFF" ] && [ "$PARAMS" == "ON" ] && [ "$ETAT" == "OFF" ]; then
  sudo $KEY\
    | $PATHBIN"leandvb" $B --fd-pp 3 --fd-info 2 --fd-const 2 $FECDVB $FASTLOCK --sr $SYMBOLRATE --standard $MODULATION --sampler rrc --rrc-steps 35 --rrc-rej 10 --roll-off 0.35 --ldpc-bf 100 -f $SR_RTLSDR >videots 3>fifo.iq &
fi

# Constellation and Parameters off
if [[ "$GRAPHICS" == "OFF" && "$PARAMS" == "OFF" ]] && [ "$ETAT" == "OFF" ]; then
  sudo $KEY\
    | $PATHBIN"leandvb" $B $FECDVB $FASTLOCK --sr $SYMBOLRATE --standard $MODULATION --sampler rrc --rrc-steps 35 --rrc-rej 10 --roll-off 0.35 --ldpc-bf 100 -f $SR_RTLSDR >videots 3>/dev/null &
fi

if [ "$ETAT" == "ON" ]; then
  sudo $KEY | (exec $PATHBIN"leandvb" $FECDVB --fd-info 2 --sr $SYMBOLRATE --standard $MODULATION --sampler rrc --rrc-steps 35 --rrc-rej 10 --roll-off 0.35 $modcod --ldpc-bf 150 --ts-udp 127.0.0.1:10010 -f $SR_RTLSDR  2>&1 1>&4 |
  (while read tag val; do
     case "$tag" in
       "LOCK" )
         case "$val" in
           0) lock="[SEARCH]"
              Lock="0" ;;
           1) lock="[LOCKED]"
              Lock="1" ;;
         esac ;;
       "FRAMELOCK" )
         case "$val" in
           0) lock="[SEARCH]"
              Lock="0" ;;
         esac ;;
       "LOCKTIME" )
          top=$(date +%s) ;;
       "MER" )
          mer=$(printf "MER %4.1f dB" "$val") ;;
       "SS" )
          ss=$(printf "SS %3.0f" "$val") ;;
     esac
  echo -ne "\r$lock\n$mer\n$ss\n" 1>&2
  Time=$(date +%s)
  tempo=$(($Time - $top))
  if [ "$Lock" == "0" ] && [ "$Lock" != "$old" ] && [ "$tempo" -gt 6 ] || [ "$Lock" == "1" ] && [ "$Lock" != "$old" ]; then
    if [ "$Lock" == "0" ]; then
      /home/pi/rpidatv/scripts/lime_ptt.sh &
      sudo killall limesdr_dvb >/dev/null 2>/dev/null
      sudo pkill -9 limesdr_dvb >/dev/null 2>/dev/null
      sudo killall netcat >/dev/null 2>/dev/null
      $PATHBIN"/limesdr_stopchannel" >/dev/null 2>/dev/null
    elif [ "$Lock" == "1" ]; then
      /home/pi/rpidatv/scripts/lime_ptt.sh &
      netcat -u -4 -l 10010 | \
      $PATHBIN"/limesdr_dvb" -s "$SYMBOLRATEK"000 -f $FECNUM/$FECDEN -r $UPSAMPLE -m $MODULATION_TX -c $CONST \
           -t "$FREQ_TX"e6 -g $LIME_TX_GAINA -q 1 -D $DIGITAL_GAIN -e $BAND_GPIO $LIMETYPE >/dev/null 2>/dev/null &
      sleep 1
    fi
    old=$Lock
  fi
done)
) 4>&1 &
fi

if [ "$ETAT" = "OFF" ]; then
  omxplayer --vol 600 --adev alsa:plughw:"$AUDIO_OUT_DEV",0 --live --layer 0 --timeout 0 videots &
fi

# Notes:
# --fd-pp FDNUM        Dump preprocessed IQ data to file descriptor
# --fd-info FDNUM      Output demodulator status to file descriptor
# --fd-const FDNUM     Output constellation and symbols to file descr
# --fd-spectrum FDNUM  Output spectrum to file descr
