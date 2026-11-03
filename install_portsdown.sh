#!/bin/bash

# Portsdown 4 Install by davecrump on 20230227

# Check current user
whoami | grep -q pi
if [ $? != 0 ]; then
  echo "Install must be performed as user pi"
  exit
fi

# Check that Portsdown is not already installed
if [ -d /home/pi/rpidatv ]; then
  echo "Portsdown already installed.  Use update instead."
  exit
fi

# Check which source needs to be loaded
GIT_SRC="f4dvk"
GIT_SRC_FILE=".portsdown_gitsrc"

if [ "$1" == "-d" ]; then
  GIT_SRC="f4dvk";
  echo
  echo "---------------------------------------------------------"
  echo "----- Installing Development version of Portsdown 4-----"
  echo "---------------------------------------------------------"
elif [ "$1" == "-u" -a ! -z "$2" ]; then
  GIT_SRC="$2"
  echo
  echo "WARNING: Installing ${GIT_SRC} development version, press enter to continue or 'q' to quit."
  read -n1 -r -s key;
  if [[ $key == q ]]; then
    exit 1;
  fi
  echo "ok!";
else
  echo
  echo "-------------------------------------------------------------"
  echo "----- Installing F4DVK Production version of Portsdown 4 ----"
  echo "-------------------------------------------------------------"
fi

# Paquets apt : un seul appel (bien plus rapide que paquet par paquet) ; si
# un paquet manque dans le depot, on reprend un par un pour installer les autres
apt_install()
{
  sudo apt-get -y install "$@" && return 0
  local p
  for p in "$@"; do
    sudo apt-get -y install "$p"
  done
}

# Compilation d'une bibliotheque : nombre de jobs selon la memoire disponible
# (environ 250 Mo par compilateur C++, au plus un par coeur), pour qu'un Pi de
# 1 Go ne sature pas sa memoire
MAKE_J=$(( ($(awk '/MemAvailable/ {print int($2/1024)}' /proc/meminfo) - 150) / 250 ))
if [ $MAKE_J -lt 1 ]; then
  MAKE_J=1
fi
if [ $MAKE_J -gt $(nproc) ]; then
  MAKE_J=$(nproc)
fi

# Download and install the VLC apt Preferences File 202212090
cd /home/pi
wget https://github.com/${GIT_SRC}/portsdown4/raw/master/scripts/configs/vlc
sudo cp vlc /etc/apt/preferences.d/vlc

# Amend the sources.list to legacy
sudo bash -c 'echo -e "deb http://legacy.raspbian.org/raspbian/ buster main contrib non-free rpi" > /etc/apt/sources.list'

# Update the package manager
echo
echo "------------------------------------"
echo "----- Updating Package Manager -----"
echo "------------------------------------"
sudo dpkg --configure -a
sudo apt-get update --allow-releaseinfo-change

# Uninstall the apt-listchanges package to allow silent install of ca certificates (201704030)
# http://unix.stackexchange.com/questions/124468/how-do-i-resolve-an-apparent-hanging-update-process
sudo apt-get -y remove apt-listchanges

# Upgrade the distribution
echo
echo "-----------------------------------"
echo "----- Performing dist-upgrade -----"
echo "-----------------------------------"
sudo apt-get -y dist-upgrade

echo
echo "Checking for EEPROM Update"
echo

sudo rpi-eeprom-update -a                            # Update will be installed on reboot if required

# Install the packages that we need
echo
echo "-------------------------------"
echo "----- Installing Packages -----"
echo "-------------------------------"

# Paquets necessaires : un seul appel a apt
PACKAGES=(
  git cmake libusb-1.0-0-dev libfftw3-dev libxcb-shape0
  wiringpi                          # Wiring pi depracated?
  libx11-dev buffer libjpeg-dev indent
  bc usbmount libvncserver-dev
  ttf-dejavu-core                   # being depracated?
  fbi netcat imagemagick omxplayer
  libvdpau-dev libva-dev            # For latest ffmpeg build
  libsqlite3-dev libi2c-dev         # 201811300 Lime
  sshpass                           # 201905090 For Jetson Nano
  libbsd-dev                        # 201910100 for raspi2raspi
  libasound2-dev sox                # 201910230 for LongMynd tone and avc2ts audio
  libavcodec-dev libavformat-dev libswscale-dev libavdevice-dev   # ffmpegsrc.cpp
  mplayer                           # 202004300 video monitor and LongMynd
  vlc                               # Latest version works for Portsdown again as of 202207130
  libcurl4-openssl-dev              # cam_ctl
  autoconf libtool                  # for fdk aac
  libxml2 libxml2-dev bison flex libcdk5-dev                      # for libiio
  libiio-dev libaio-dev libserialport-dev libavahi-client-dev     # for libiio
  nginx-light                       # For web access
  libfcgi-dev                       # For web control
  libairspy-dev                     # For Airspy Bandviewer
  expect                            # For unattended installs
  uhubctl                           # For SDRPlay USB resets
  libssl-dev                        # For websockets
  libzstd-dev                       # For libiio 202309040
  arp-scan                          # For List Network Devices
  cppcheck                          # For HamTV Merger Client
  socat                             # For debugging network issues
  lsof                              # For debugging web control issues
  python3-dev python3-pip           # For ISS tracker
  libhamlib-dev libhamlib++-dev libhamlib-utils                   # For ISS tracker
  nodejs npm                        # streaming audio
  ffmpeg
  pi-bluetooth bluealsa
  picotool                          # Flasheur de RP2040
  libsndfile1-dev fftw3-dev liblapack-dev portaudio19-dev         # for dsd
  gcc build-essential perl sendemail                              # for 406 decoder
)
apt_install "${PACKAGES[@]}"

python3 -c "import ephem, requests" 2>/dev/null || pip3 install ephem requests   # For ISS tracker

sudo cp /usr/bin/ffmpeg /usr/bin/ffmpeg2
sudo cp /usr/bin/aplay /usr/bin/aplay2

sudo adduser pi bluetooth
sudo cp /lib/systemd/system/bluetooth.service /lib/systemd/system/bluetooth.service-org
sudo sed -i 's|^ExecStart=/usr/lib/bluetooth/bluetoothd$|ExecStart=/usr/lib/bluetooth/bluetoothd --noplugin=sap|' /lib/systemd/system/bluetooth.service


# Install WiringPi
cd /home/pi
git clone --depth 1 https://github.com/WiringPi/WiringPi.git
cd WiringPi
./build debian

# Read latest WiringPi version number and install it
vMaj=`cut -d. -f1 VERSION`
vMin=`cut -d. -f2 VERSION`
mv debian-template/wiringpi_"$vMaj"."$vMin"_armhf.deb .
sudo apt install ./wiringpi_"$vMaj"."$vMin"_armhf.deb
cd /home/pi

# Install libiio for Pluto SigGen (and Langstone)
cd /home/pi
git clone https://github.com/analogdevicesinc/libiio.git
cd libiio
git reset --hard b6028fdeef888ab45f7c1dd6e4ed9480ae4b55e3  # Back to Version 0.25
cmake -DWITH_EXAMPLES=ON ./
make -j$MAKE_J all
sudo make install
cd /home/pi

# Install SoapySDR
git clone --depth 1 https://github.com/pothosware/SoapySDR.git
cd SoapySDR
mkdir build && cd build
cmake ..
make -j$MAKE_J
sudo make install
cd /home/pi

# Install SoapyPlutoSDR
git clone --depth 1 https://github.com/pothosware/SoapyPlutoSDR.git
cd SoapyPlutoSDR
mkdir build && cd build
cmake ..
make -j$MAKE_J
sudo make install
cd /home/pi

sudo rm -r SoapySDR
sudo rm -r SoapyPlutoSDR

git clone --depth 1 https://github.com/ha7ilm/csdr.git
cd csdr
make -j$MAKE_J
sudo make install
cd /home/pi

sudo rm -r csdr

# Install Websockets for Meteor Beacon RX server
git clone --depth 1 -b v4.5-stable --single-branch https://github.com/warmcat/libwebsockets.git
cd libwebsockets
cmake ./
make -j$MAKE_J all
sudo make install
sudo ldconfig
cd /home/pi


git clone --depth 1 https://github.com/szechyjs/mbelib.git
cd mbelib
mkdir build && cd build
cmake ..
make -j$MAKE_J
sudo make install
sudo ldconfig
cd /home/pi
sudo rm -r mbelib

wget -O itpp-latest.tar.bz2 http://sourceforge.net/projects/itpp/files/latest/download?source=files
tar xjf itpp-latest.tar.bz2
cd itpp-4.3.1
mkdir build && cd build
cmake ..
make -j$MAKE_J
sudo make install
sudo ldconfig
cd /home/pi
sudo rm -r itpp-4.3.1

git clone --depth 1 https://github.com/f4exb/dsdcc.git
cd dsdcc
mkdir build && cd build
cmake -DUSE_MBELIB=ON ..
make -j$MAKE_J
sudo make install
cd /home/pi
sudo rm -r dsdcc

git clone --depth 1 https://github.com/f4dvk/dsd.git
cd dsd
mkdir build && cd build
cmake ..
make -j$MAKE_J
sudo make install
cd /home/pi
sudo rm -r dsd

echo
echo "----------------------------------"
echo "--- Installing WM8960 Drivers ----"
echo "----------------------------------"
git clone --depth 1 https://github.com/waveshare/WM8960-Audio-HAT
cd WM8960-Audio-HAT
sudo chmod +x install.sh
sudo ./install.sh

cd /home/pi
sudo rm -r WM8960-Audio-HAT

# Enable USB Storage automount in Buster
echo
echo "----------------------------------"
echo "----- Enabling USB Automount -----"
echo "----------------------------------"
cd /lib/systemd/system/
if ! grep -q PrivateMounts=no systemd-udevd.service; then
  sudo sed -i -e 's/PrivateMounts=yes/PrivateMounts=no/' systemd-udevd.service
fi

cd /home/pi

# Set auto login to command line.
sudo raspi-config nonint do_boot_behaviour B2

# set the framebuffer to 32 bit depth by disabling dtoverlay=vc4-fkms-v3d
echo
echo "----------------------------------------------"
echo "---- Setting Framebuffer to 32 bit depth -----"
echo "----------------------------------------------"

sudo sed -i "/^dtoverlay=vc4-fkms-v3d/c\#dtoverlay=vc4-fkms-v3d" /boot/config.txt

# Enable camera
echo
echo "--------------------------------------------------"
echo "---- Enabling the Pi Cam in /boot/config.txt -----"
echo "--------------------------------------------------"
sudo bash -c 'echo -e "\n##Enable Pi Camera" >> /boot/config.txt'
sudo bash -c 'echo -e "\nstart_x=1\ngpu_mem=128\n" >> /boot/config.txt'


# Reduce the dhcp client timeout to speed off-network startup
echo
echo "-------------------------------------------"
echo "---- Reducing the dhcp client timeout -----"
echo "-------------------------------------------"
sudo bash -c 'echo -e "\n# Shorten dhcpcd timeout from 30 to 5 secs" >> /etc/dhcpcd.conf'
sudo bash -c 'echo -e "timeout 5\n" >> /etc/dhcpcd.conf'
cd /home/pi/

# Download the previously selected version of Portsdown 4
echo
echo "--------------------------------------------"
echo "----- Downloading Portsdown 4 Software -----"
echo "--------------------------------------------"
wget https://github.com/${GIT_SRC}/portsdown4/archive/master.zip

# Unzip the rpidatv software and copy to the Pi
unzip -o master.zip
mv portsdown4-master rpidatv
rm master.zip
cd /home/pi


# Install LimeSuite 22.09 as at 27 Feb 23
# Commit 9c983d872e75214403b7778122e68d920d583add
echo
echo "--------------------------------------"
echo "----- Installing LimeSuite 22.09 -----"
echo "--------------------------------------"
wget https://github.com/myriadrf/LimeSuite/archive/9c983d872e75214403b7778122e68d920d583add.zip -O master.zip
unzip -o master.zip
cp -f -r LimeSuite-9c983d872e75214403b7778122e68d920d583add LimeSuite
rm -rf LimeSuite-9c983d872e75214403b7778122e68d920d583add
rm master.zip

# Compile LimeSuite
cd LimeSuite/
mkdir dirbuild
cd dirbuild/
cmake ../
make -j$MAKE_J
sudo make install
sudo ldconfig
cd /home/pi

# Install udev rules for LimeSuite
cd LimeSuite/udev-rules
chmod +x install.sh
sudo /home/pi/LimeSuite/udev-rules/install.sh
cd /home/pi

# Record the LimeSuite Version
echo "9c983d8" >/home/pi/LimeSuite/commit_tag.txt

# Download the LimeSDR Mini firmware/gateware versions
echo
echo "------------------------------------------------------"
echo "----- Downloading LimeSDR Mini Firmware versions -----"
echo "------------------------------------------------------"

# Current LimeSDR Mini V1 Version from LimeSuite 22.09
mkdir -p /home/pi/.local/share/LimeSuite/images/22.09/
wget https://downloads.myriadrf.org/project/limesuite/22.09/LimeSDR-Mini_HW_1.2_r1.30.rpd -O \
               /home/pi/.local/share/LimeSuite/images/22.09/LimeSDR-Mini_HW_1.2_r1.30.rpd

# DVB-S/S2 Version
mkdir -p /home/pi/.local/share/LimeSuite/images/v0.3
wget https://github.com/natsfr/LimeSDR_DVBSGateware/releases/download/v0.3/LimeSDR-Mini_lms7_trx_HW_1.2_auto.rpd -O \
 /home/pi/.local/share/LimeSuite/images/v0.3/LimeSDR-Mini_lms7_trx_HW_1.2_auto.rpd

# Current LimeSDR Mini V2 Version from LimeSuite 23.11
wget https://downloads.myriadrf.org/project/limesuite/23.11/LimeSDR-Mini_HW_2.0_r2.2.bit -O \
               /home/pi/.local/share/LimeSuite/images/22.09/LimeSDR-Mini_HW_2.0_r2.2.bit

# Check that it was downloaded, if not, go to source and get it
if [[ "$?" != "0" ]]; then
  rm /home/pi/.local/share/LimeSuite/images/22.09/LimeSDR-Mini_HW_2.0_r2.2.bit
  wget https://github.com/myriadrf/LimeSDR-Mini-v2_GW/raw/main/LimeSDR-Mini_bitstreams/lms7_trx_impl1.bit -O \
    /home/pi/.local/share/LimeSuite/images/22.09/LimeSDR-Mini_HW_2.0_r2.2.bit
fi

echo
echo "--------------------------------------------------------------"
echo "----- Downloading Portsdown 4 version of avc2ts Software -----"
echo "--------------------------------------------------------------"

# Download the previously selected version of avc2ts for Portsdown 4
cd /home/pi
#wget https://github.com/${GIT_SRC}/avc2ts/archive/refs/heads/portsdown4.zip
wget https://github.com/BritishAmateurTelevisionClub/avc2ts/archive/refs/heads/portsdown4.zip

# Unzip the avc2ts software and copy to the Pi
unzip -o portsdown4.zip
mv avc2ts-portsdown4 avc2ts
rm portsdown4.zip

# Build avc2ts and dependencies
echo
echo "--------------------------------------------"
echo "----- Building avc2ts and dependencies -----"
echo "--------------------------------------------"

# For libmpegts
cd /home/pi/avc2ts
git clone --depth 1 https://github.com/F5OEO/libmpegts.git
cd libmpegts
# Overwrite updated config version files 202307170
cp /home/pi/rpidatv/scripts/configs/config.guess config.guess
cp /home/pi/rpidatv/scripts/configs/config.sub config.sub
./configure
make -j$MAKE_J
cd ../

# For libfdkaac
git clone --depth 1 https://github.com/mstorsjo/fdk-aac.git
cd fdk-aac
# Overwrite updated config version files 202307170
cp /home/pi/rpidatv/scripts/configs/config.guess config.guess
cp /home/pi/rpidatv/scripts/configs/config.sub config.sub
./autogen.sh
./configure
make -j$MAKE_J && sudo make install
sudo ldconfig
cd ../

#libyuv should be used for fast picture transformation : not yet implemented
git clone --depth 1 https://chromium.googlesource.com/libyuv/libyuv
cd libyuv
#should patch linux.mk with -DHAVE_JPEG on CXX and CFLAGS
#seems to be link with libjpeg9-dev
make -j$MAKE_J V=1 -f linux.mk
cd ../

echo
echo "-----------------------------------------------"
echo "----- Installing RTL-SDR Drivers and Apps -----"
echo "-----------------------------------------------"
cd /home/pi
git clone --depth 1 https://github.com/osmocom/rtl-sdr.git

# Compile and install rtl-sdr
cd rtl-sdr/ && mkdir build && cd build
cmake ../ -DINSTALL_UDEV_RULES=ON
make -j$MAKE_J && sudo make install && sudo ldconfig
sudo bash -c 'echo -e "\n# for RTL-SDR:\nblacklist dvb_usb_rtl28xxu\n" >> /etc/modprobe.d/blacklist.conf'
cd /home/pi

# Download, compile and install DATV Express-server
echo
echo "------------------------------------------"
echo "----- Installing DATV Express Server -----"
echo "------------------------------------------"
cd /home/pi
wget https://github.com/G4GUO/express_server/archive/master.zip
unzip master.zip
mv express_server-master express_server
rm master.zip
cd /home/pi/express_server
make -j$MAKE_J
sudo make install

cd /home/pi
# Download, compile and install the executable for hardware shutdown button
echo
echo "------------------------------------------------------------"
echo "----- Installing the hardware shutdown Button software -----"
echo "------------------------------------------------------------"

git clone --depth 1 https://github.com/philcrump/pi-sdn /home/pi/pi-sdn-build

# Install new version that sets swapoff
cp -f /home/pi/rpidatv/src/pi-sdn/main.c /home/pi/pi-sdn-build/main.c
cd /home/pi/pi-sdn-build
make -j$MAKE_J
mv pi-sdn /home/pi/
cd /home/pi


# Reduce the dhcp client timeout to speed off-network startup (201704160)
#sudo bash -c 'echo -e "\n# Shorten dhcpcd timeout from 30 to 5 secs" >> /etc/dhcpcd.conf'
#sudo bash -c 'echo -e "\ntimeout 5\n" >> /etc/dhcpcd.conf'

echo
echo "--------------------------"
echo "----- Preparing WiFi -----"
echo "--------------------------"

sudo rm /etc/wpa_supplicant/wpa_supplicant.conf
sudo cp /home/pi/rpidatv/scripts/configs/wpa_supplicant.conf /etc/wpa_supplicant/wpa_supplicant.conf
sudo chown root /etc/wpa_supplicant/wpa_supplicant.conf

# Disable Wifi on Initial Start
cp ~/rpidatv/scripts/configs/text.wifi_off ~/.wifi_off >/dev/null 2>/dev/null

# Compile the Portsdown programs
echo
echo "---------------------------------------------"
echo "----- Compiling the Portsdown programs ------"
echo "---------------------------------------------"
# Programmes independants compiles en parallele, selon les coeurs et la memoire
# disponible (scripts/build_jobs.sh) ; journaux dans /home/pi/build_logs
source /home/pi/rpidatv/scripts/build_jobs.sh
build_portsdown_programs
cd /home/pi

# Compile Langstone
echo
echo "------------------------------------------"
echo "----------- Compiling Langstone ----------"
echo "------------------------------------------"
/home/pi/rpidatv/add_langstone2.sh
cd /home/pi

echo
echo "------------------------------------------"
echo "----- Setting up for captured images -----"
echo "------------------------------------------"

# Amend /etc/fstab to create a tmpfs drive at ~/tmp for multiple images (201708150)
sudo sed -i '4itmpfs           /home/pi/tmp    tmpfs   defaults,noatime,nosuid,size=10m  0  0' /etc/fstab

# Create a ~/snaps folder for captured images (201708150)
mkdir /home/pi/snaps

# Set the image index number to 0 (201708150)
echo "0" > /home/pi/snaps/snap_index.txt


# Download and compile the components for Screen Capture
wget https://github.com/AndrewFromMelbourne/raspi2png/archive/master.zip
unzip master.zip
mv raspi2png-master raspi2png
rm master.zip
cd raspi2png
make -j$MAKE_J
sudo make install
cd /home/pi

echo
echo "--------------------------------------"
echo "----- Configure the Menu Aliases -----"
echo "--------------------------------------"

# Install the menu aliases
echo "alias menu='/home/pi/rpidatv/scripts/menu.sh menu'" >> /home/pi/.bash_aliases
echo "alias gui='/home/pi/rpidatv/scripts/utils/guir.sh'"  >> /home/pi/.bash_aliases
echo "alias ugui='/home/pi/rpidatv/scripts/utils/uguir.sh'"  >> /home/pi/.bash_aliases
echo "alias udvbt='/home/pi/rpidatv/scripts/utils/udvbt.sh'"  >> /home/pi/.bash_aliases
echo "alias stop='/home/pi/rpidatv/scripts/utils/stop.sh'"  >> /home/pi/.bash_aliases

# Modify .bashrc to run startup script on ssh logon
#cd /home/pi
#sed -i 's|/home/pi/Langstone/run|source /home/pi/rpidatv/scripts/startup.sh|' .bashrc

echo if test -z \"\$SSH_CLIENT\""; then" >> ~/.bashrc
echo "  source /home/pi/rpidatv/scripts/startup.sh" >> ~/.bashrc
echo fi >> ~/.bashrc

#Configure the boot parameters

if !(grep lcd_rotate /boot/config.txt) then
  sudo sh -c "echo #lcd_rotate=2 >> /boot/config.txt"
fi
if !(grep disable_splash /boot/config.txt) then
  sudo sh -c "echo disable_splash=1 >> /boot/config.txt"
fi
if !(grep global_cursor_default /boot/cmdline.txt) then
  sudo sed -i '1s,$, vt.global_cursor_default=0,' /boot/cmdline.txt
fi

# Streaming audio source: https://github.com/JoJoBond/3LAS

cd /home/pi/rpidatv/server/
npm install ws wrtc
chmod ug+x stream.sh
cd /home/pi

cp /home/pi/rpidatv/scripts/configs/asoundrc /home/pi/.asoundrc
sudo sed -i '$ s/$/\nsnd-aloop/' /etc/modules

# Configure the nginx web server
cp -r /home/pi/rpidatv/scripts/configs/webroot /home/pi/webroot
sudo cp /home/pi/rpidatv/scripts/configs/nginx.conf /etc/nginx/nginx.conf

# Create a directory for IQ files 202403250
mkdir /home/pi/iqfiles

# Record Version Number
cd /home/pi/rpidatv/scripts/
cp latest_version.txt installed_version.txt
cd /home/pi

# Save git source used
echo "${GIT_SRC}" > /home/pi/${GIT_SRC_FILE}

echo
echo "SD Card Serial:"
cat /sys/block/mmcblk0/device/cid

# Installation décodage 406
cd /home/pi/rpidatv/406
./install.sh

# installation de hostapd et dnsmasq
sudo apt-get -f -y install hostapd
sudo apt-get -f -y install dnsmasq

sudo systemctl disable hostapd
sudo systemctl disable dnsmasq
sudo service hostapd stop
sudo service dnsmasq stop

# Réduction temps démarrage sans ethernet
sudo sed -i 's/^TimeoutStartSec.*/TimeoutStartSec=5/' /etc/systemd/system/network-online.target.wants/networking.service
sudo sed -i 's/^#timeout.*/timeout 8;/' /etc/dhcp/dhclient.conf
sudo sed -i 's/^#retry.*/retry 20;/' /etc/dhcp/dhclient.conf

sudo sed -i 's/^#host-name=foo.*/host-name=rpidatv4/' /etc/avahi/avahi-daemon.conf

# Reboot
echo
echo "--------------------------------"
echo "----- Complete.  Rebooting -----"
echo "--------------------------------"
sleep 1

sudo reboot now
exit
