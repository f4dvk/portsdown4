#!/bin/bash

ssid()
{
  iwgetid >/dev/null 2>/dev/null > /home/pi/ssid1.txt
}

Hotspot()
{
  sudo service hostapd status >/dev/null 2>/dev/null

  if [ $? == 0 ]; then
    echo "ssid=Hotspot" > /home/pi/rpidatv/scripts/wifi_get.txt
  else
    echo "ssid=Deconnecte" > /home/pi/rpidatv/scripts/wifi_get.txt
  fi
}

sudo rm /home/pi/rpidatv/scripts/wifi_get.txt

ssid

if [ $? != 0 ]; then
  Hotspot
  sudo rm /home/pi/ssid1.txt
  exit
fi

while [ $? == 0 ] && [ -z "/home/pi/ssid1.txt" ]; do
  ssid
done

cat /home/pi/ssid1.txt | sed 's/.* //;s/ESSID/ssid/g;s/ //g;s/""/Déconnecté/g;s/"//g;s/:/=/g' > /home/pi/rpidatv/scripts/wifi_get.txt

sudo rm /home/pi/ssid1.txt

if [ -s "/home/pi/rpidatv/scripts/wifi_get.txt" ];then
  exit
else
  echo "ssid=Deconnecte" > /home/pi/rpidatv/scripts/wifi_get.txt
fi

exit
