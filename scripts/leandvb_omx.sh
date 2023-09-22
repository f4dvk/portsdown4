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

############ IDENTIFY RPi JACK AUDIO CARD NUMBER #############################

# List the audio playback devices, select the line for the Headphones device:
# card 0: Headphones [bcm2835 Headphones], device 0: bcm2835 Headphones [bcm2835 Headphones]
# then take the 6th character

# If headphones not found, look for bcm2835 ALSA:
# card 0: ALSA [bcm2835 ALSA], device 0: bcm2835 ALSA [bcm2835 ALSA]
# and take 6th character

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

############ IDENTIFY USB DONGLE AUDIO CARD NUMBER #############################

# List the audio playback devices, select the line for the audio dongle device:
# card 1: Device [USB Audio Device], device 0: USB Audio [USB Audio]
# then take the 6th character

USBOUT_AUDIO_DEV="$(aplay -l 2> /dev/null | grep 'USB Audio' | cut -c6-6)"

if [ "$USBOUT_AUDIO_DEV" == '' ]; then
  printf "USB Dongle audio device was not found, setting to RPi Jack\n"
  USBOUT_AUDIO_DEV=$RPIJ_AUDIO_DEV
fi

# Take only the first character
USBOUT_AUDIO_DEV="$(echo $USBOUT_AUDIO_DEV | cut -c1-1)"

echo "The USB Dongle Audio Card number is -"$USBOUT_AUDIO_DEV"-"

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
  elif [ "$SYMBOLRATEK" -gt 249 ] && [ "$SYMBOLRATEK" -lt 500 ] && [ "$SDR" == "LIMEMINI" ]; then
    SR_RTLSDR=850000
  else
    SR_RTLSDR=2400000
  fi
else
  let SR_RTLSDR=SAMPLERATEK*1000
fi

AUDIO_OUT=$(get_config_var audio $RCONFIGFILE)

if [ "$AUDIO_OUT" == "rpi" ]; then
  AUDIO_OUT_DEV=$RPIJ_AUDIO_DEV
else
  AUDIO_OUT_DEV=$USBOUT_AUDIO_DEV
fi

echo "The Selected Audio Card number is -"$AUDIO_OUT_DEV"-"

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

sudo rm videots >/dev/null 2>/dev/null
sudo killall omxplayer.bin >/dev/null 2>/dev/null
mkfifo videots

sudo $KEY\
      | $PATHBIN"leandvb" $B --fd-info 3 $FECDVB $FASTLOCK --sr $SYMBOLRATE --standard $MODULATION --sampler rrc --rrc-steps 35 --rrc-rej 10 --roll-off 0.35 --ldpc-bf 100 -f $SR_RTLSDR >videots 2>/dev/null &

omxplayer --vol 600 --adev alsa:plughw:"$AUDIO_OUT_DEV",0 \
  --live --layer 6 videots &

exit
