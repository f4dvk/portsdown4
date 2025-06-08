#!/bin/bash

PATHBIN="/home/pi/rpidatv/bin/"
PATHSCRIPT="/home/pi/rpidatv/scripts"
PCONFIGFILE="/home/pi/rpidatv/scripts/portsdown_config.txt"
RCONFIGFILE="/home/pi/rpidatv/scripts/longmynd_config.txt"
RXPRESETSFILE="/home/pi/rpidatv/scripts/rx_presets.txt"
RTLPRESETSFILE="/home/pi/rpidatv/scripts/rtl-fm_presets.txt"

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

SYMBOLRATEK=$(get_config_var rx0sr $RXPRESETSFILE)
let SYMBOLRATE=SYMBOLRATEK*1000

MODULATION=$(get_config_var rx0modulation $RXPRESETSFILE)
FEC=$(get_config_var rx0fec $RXPRESETSFILE)
SDR=$(get_config_var rx0sdr $RXPRESETSFILE)
SAMPLERATEK=$(get_config_var rx0samplerate $RXPRESETSFILE)
GAIN=$(get_config_var rx0gain $RXPRESETSFILE)

AUDIO_OUT=$(get_config_var audio $RCONFIGFILE)

DISPLAY=$(get_config_var display $PCONFIGFILE)

FREQ_OUTPUT=$(get_config_var rx0frequency $RXPRESETSFILE)
FreqHz=$(echo "($FREQ_OUTPUT*1000000)/1" | bc )

if [ "$FEC" != "Auto" ]; then
 let FECNUM=FEC
 let FECDEN=FEC+1
 FECDVB="--cr $FECNUM"/"$FECDEN"
else
 FECDVB=""
fi

if [ "$SAMPLERATEK" == "0" ]; then
  if [ "$SYMBOLRATEK" -lt 250 ]; then
    SR_RTLSDR=300000
  elif [ "$SYMBOLRATEK" -gt 249 ] && [ "$SYMBOLRATEK" -lt 500 ] && [ "$SDR" == "RTLSDR" ]; then
    SR_RTLSDR=1000000
  elif [ "$SYMBOLRATEK" -gt 499 ] && [ "$SYMBOLRATEK" -lt 1000 ]; then
    SR_RTLSDR=1200000
  elif [ "$SYMBOLRATEK" -gt 999 ] && [ "$SYMBOLRATEK" -lt 1101 ]; then
    SR_RTLSDR=1250000
  elif [ "$SYMBOLRATEK" -gt 249 ] && [ "$SYMBOLRATEK" -lt 500 ] && [ "$SDR" != "RTLSDR" ]; then
    SR_RTLSDR=850000
  else
    SR_RTLSDR=2400000
  fi
else
  let SR_RTLSDR=SAMPLERATEK*1000
fi

if [ "$AUDIO_OUT" == "rpi" ]; then
  # Check for latest Buster update
  aplay -l | grep -q 'bcm2835 Headphones'
  if [ $? == 0 ]; then
    AUDIO_DEVICE="hw:CARD=Headphones,DEV=0"
  else
    AUDIO_DEVICE="hw:CARD=ALSA,DEV=0"
  fi
else
  AUDIO_DEVICE="hw:CARD=Device,DEV=0"
fi
if [ "$AUDIO_OUT" == "hdmi" ]; then
  # Overide for HDMI
  AUDIO_DEVICE="hw:CARD=b1,DEV=0"
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

FREQOFFSET=$(get_config_var roffset $RTLPRESETSFILE)

if [ "$SDR" == "RTLSDR" ]; then
  KEY="rtl_sdr -p $FREQOFFSET -g $GAIN -f $FreqHz -s $SR_RTLSDR - 2>/dev/null "
  B=""
fi
if [ "$SDR" == "LIMEMINI" ]; then
  KEY="/home/pi/rpidatv/bin/limesdr_dump -f $FreqHz -b 2.5e6 -s $SR_RTLSDR -r $UPSAMPLE_RX -g $GAIN_LIME -l 1024*1024 | buffer"
  B="--s12"
fi
if [ "$SDR" == "PLUTOSDR" ]; then
  KEY="rx_sdr -I CS16 -F CS16 -s $SR_RTLSDR -f $FreqHz - 2>/dev/null "
  B="--s16"
fi

# Pluto
# Phonie : rx_sdr -F CF32 -s 1200000 -f 430500000 - | csdr fir_decimate_cc 25 0.005 HAMMING | csdr fmdemod_quadri_cf  | csdr limit_ff | csdr deemphasis_nfm_ff 48000 | csdr fastagc_ff | csdr convert_f_i16  | aplay -f S16_LE -c1 -r48000
# DATV : rx_sdr -I CS16 -F CS16 -s 1200000 -f 437000000 -

sudo rm fifo.264 >/dev/null 2>/dev/null
sudo rm videots >/dev/null 2>/dev/null
sudo killall leandvb >/dev/null 2>/dev/null
echo " " >/home/pi/tmp/vlc_overlay.txt
sudo killall vlc >/dev/null 2>/dev/null
mkfifo fifo.264
mkfifo videots

if [[ ! -f /home/pi/tmp/vlcprimed ]]; then
  cvlc -I rc --rc-host 127.0.0.1:1111 -f --codec ffmpeg --video-title-timeout=100 \
    --width 800 --height 480 \
    --sub-filter marq --marq-x 25 --marq-file "/home/pi/tmp/vlc_overlay.txt" \
    --gain 3 --alsa-audio-device $AUDIO_DEVICE \
    /home/pi/rpidatv/video/blank.ts vlc:quit >/dev/null 2>/dev/null &
  sleep 1
  touch /home/pi/tmp/vlcprimed
  echo shutdown | nc 127.0.0.1 1111
fi

# Make sure that the screen background is all black
sudo killall fbi >/dev/null 2>/dev/null
sudo fbi -T 1 -noverbose -a $PATHSCRIPT"/images/Blank_Black.png"
(sleep 0.2; sudo killall -9 fbi >/dev/null 2>/dev/null) &  ## kill fbi once it has done its work

sudo $KEY\
      | $PATHBIN"leandvb" $B --fd-info 3 $FECDVB $FASTLOCK --sr $SYMBOLRATE --standard $MODULATION --sampler rrc --rrc-steps 35 --rrc-rej 10 --roll-off 0.35 --ldpc-bf 150 -f $SR_RTLSDR >videots 2>/dev/null &

cvlc -I rc --rc-host 127.0.0.1:1111 -f --codec ffmpeg --video-title-timeout=100 \
  --width 800 --height 480 \
  --sub-filter marq --marq-x 25 --marq-file "/home/pi/tmp/vlc_overlay.txt" \
  --gain 3 --alsa-audio-device $AUDIO_DEVICE \
  videots >/dev/null 2>/dev/null &
