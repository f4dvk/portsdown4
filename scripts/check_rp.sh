#!/bin/bash
# Check for the presence of an RP2040
# Echo 0 if present, echo 1 if not detected

if [ "$1" == "-f" ]; then
  sudo picotool info | grep -q "Program Information"
  if [ $? == 0 ]; then   ## Present
    echo "0"
  else
    echo "1"
  fi
elif [ "$1" == "-load" ]; then
  sudo picotool load /home/pi/rpidatv/scripts/configs/sirene.uf2 | grep 100%
  if [ $? == 0 ]; then   ## Load OK
    sudo picotool reboot
    echo "0"
  else
    echo "1"
  fi
else
  lsusb | grep -E -q "2e8a:f00a"
  if [ $? == 0 ]; then   ## Present
    echo "0"
  else
    echo "1"
  fi
fi

exit
