#!/bin/bash

# set -x

cd /home/pi

# Stop Langstone if it is running (it shouldn't be)
# /home/pi/Langstone/stop_both
# Without frills, this has the same effect:

killall python >/dev/null 2>&1
killall /home/pi/Langstone/GUI_Lime >/dev/null 2>&1
killall /home/pi/Langstone/GUI_Pluto >/dev/null 2>&1
killall /home/pi/Langstone/GUI_RtlSdr >/dev/null 2>&1
killall /home/pi/Langstone/GUI_Rtlhf >/dev/null 2>&1

# Keep a copy of the old Config files
cp -f /home/pi/Langstone/Langstone_Lime.conf /home/pi/rpidatv/scripts/configs/Langstone_Lime.conf
cp -f /home/pi/Langstone/Langstone_Pluto.conf /home/pi/rpidatv/scripts/configs/Langstone_Pluto.conf
cp -f /home/pi/Langstone/Langstone_RtlSdr.conf /home/pi/rpidatv/scripts/configs/Langstone_RtlSdr.conf
cp -f /home/pi/Langstone/Langstone_Rtlhf.conf /home/pi/rpidatv/scripts/configs/Langstone_Rtlhf.conf

# Remove the old Langstone build
rm -rf /home/pi/Langstone

sudo apt-get -y install gr-osmosdr

mkdir /home/pi/Langstone
cp -f /home/pi/rpidatv/src/langstone/* /home/pi/Langstone/

# Restore the old Config files
cp -f /home/pi/rpidatv/scripts/configs/Langstone_Lime.conf /home/pi/Langstone/Langstone_Lime.conf
cp -f /home/pi/rpidatv/scripts/configs/Langstone_Pluto.conf /home/pi/Langstone/Langstone_Pluto.conf
cp -f /home/pi/rpidatv/scripts/configs/Langstone_RtlSdr.conf /home/pi/Langstone/Langstone_RtlSdr.conf
cp -f /home/pi/rpidatv/scripts/configs/Langstone_Rtlhf.conf /home/pi/Langstone/Langstone_Rtlhf.conf

chmod +x /home/pi/Langstone/build
chmod +x /home/pi/Langstone/run_lime
chmod +x /home/pi/Langstone/stop_lime
chmod +x /home/pi/Langstone/run_pluto
chmod +x /home/pi/Langstone/stop_pluto
chmod +x /home/pi/Langstone/run_RtlSdr
chmod +x /home/pi/Langstone/stop_RtlSdr
chmod +x /home/pi/Langstone/run_Rtlhf
chmod +x /home/pi/Langstone/stop_Rtlhf
chmod +x /home/pi/Langstone/update
chmod +x /home/pi/Langstone/set_pluto
chmod +x /home/pi/Langstone/set_sdr
chmod +x /home/pi/Langstone/set_sound
chmod +x /home/pi/Langstone/run_both
chmod +x /home/pi/Langstone/stop_both

/home/pi/Langstone/build

cd /home/pi

if [ ! $(find /home/pi -name gr-dsd) ]; then
  echo "#################################"
  echo "##         Install DSD         ##"
  echo "#################################"
  sudo apt-get -y install libsndfile1-dev libboost-all-dev libcppunit-dev libitpp-dev liblog4cpp5-dev swig
  git clone https://github.com/argilo/gr-dsd
  cd gr-dsd
  git checkout ab4a739
  mkdir build
  cd build
  cmake ..
  make
  sudo make install
  sudo ldconfig
  cd /home/pi
fi

exit
