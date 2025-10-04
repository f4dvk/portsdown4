#!/bin/bash

# Install script to add Langstone V2 to Portsdown 4

############ Set Environment Variables ###############

PCONFIGFILE="/home/pi/rpidatv/scripts/portsdown_config.txt"

############ Function to Write to Config File ###############

set_config_var() {
lua - "$1" "$2" "$3" <<EOF > "$3.bak"
local key=assert(arg[1])
local value=assert(arg[2])
local fn=assert(arg[3])
local file=assert(io.open(fn))
local made_change=false
for line in file:lines() do
if line:match("^#?%s*"..key.."=.*$") then
line=key.."="..value
made_change=true
end
print(line)
end
if not made_change then
print(key.."="..value)
end
EOF
mv "$3.bak" "$3"
}

############################################################

# First, update packages to the latest standard

sudo dpkg --configure -a                         # Make sure that all the packages are properly configured
sudo apt-get clean                               # Clean up the old archived packages
sudo apt-get update --allow-releaseinfo-change   # Update the package list
sudo apt-get -y dist-upgrade                     # Upgrade all the installed packages to their latest version

# Langstone packages to install
sudo apt-get -y install gr-iio
sudo apt-get -y install gnuradio
sudo apt-get -y install raspi-gpio
sudo apt-get -y install doxygen
sudo apt-get -y install swig

cd /home/pi

echo "#################################"
echo "##        Install gr-limesdr   ##"
echo "#################################"

git clone https://github.com/myriadrf/gr-limesdr.git
cd gr-limesdr
mkdir build
cd build
cmake ..
make
sudo make install
sudo ldconfig
cd /home/pi

sudo apt-get -y install gr-osmosdr

echo
# Install libiio and dependencies if required (may already be there for Pluto SigGen)
if [ ! -d  /home/pi/libiio ]; then
  echo "Installing libiio"
  echo
  git clone https://github.com/analogdevicesinc/libiio.git
  cd libiio
  git reset --hard b6028fdeef888ab45f7c1dd6e4ed9480ae4b55e3  # Back to Version 0.25
  cmake ./
  make all
  sudo make install
  cd /home/pi
else
  echo "Found libiio installed"
  echo
fi

# Enable i2c support
sudo raspi-config nonint do_i2c 0

echo "###################################"
echo "##     Installing Langstone V2   ##"
echo "###################################"

# Delete Langstone V1
sudo rm -rf /home/pi/Langstone >/dev/null 2>/dev/null

cd /home/pi
mkdir /home/pi/Langstone
cp -f /home/pi/rpidatv/src/langstone/* /home/pi/Langstone/
cd Langstone
chmod +x build
chmod +x run_lime
chmod +x stop_lime
chmod +x run_pluto
chmod +x stop_pluto
chmod +x run_RtlSdr
chmod +x stop_RtlSdr
chmod +x run_Rtlhf
chmod +x stop_Rtlhf
chmod +x update
chmod +x set_pluto
chmod +x set_sdr
chmod +x set_sound
chmod +x run_both
chmod +x stop_both
./build

cd /home/pi

# Record the correct version in the config file
set_config_var langstone v2lime $PCONFIGFILE

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

#echo "#################################"
#echo "##       Reboot and Start      ##"
#echo "#################################"

#Reboot and start
#sudo reboot now
