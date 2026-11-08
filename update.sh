#!/bin/bash

# Updated by davecrump 20230228 for Portsdown 4 and LimeSDR Mini V2

DisplayUpdateMsg() {
  # Delete any old update message image
  rm /home/pi/tmp/update.jpg >/dev/null 2>/dev/null

  # Create the update image in the tempfs folder
  convert -font "FreeSans" -size 800x480 xc:white \
    -gravity North -pointsize 40 -annotate 0 "Updating Portsdown Software" \
    -gravity Center -pointsize 50 -annotate 0 "$1""\n\nPlease wait" \
    -gravity South -pointsize 50 -annotate 0 "DO NOT TURN POWER OFF" \
    /home/pi/tmp/update.jpg

  # Display the update message on the desktop
  sudo fbi -T 1 -noverbose -a /home/pi/tmp/update.jpg >/dev/null 2>/dev/null
  (sleep 1; sudo killall -9 fbi >/dev/null 2>/dev/null) &  ## kill fbi once it has done its work
  /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &
}

DisplayRebootMsg() {
  # Delete any old update message
  rm /home/pi/tmp/update.jpg >/dev/null 2>/dev/null

  # Create the update image in the tempfs folder
  convert -font "FreeSans" -size 800x480 xc:white \
    -gravity North -pointsize 40 -annotate 0 "Updating Portsdown Software" \
    -gravity Center -pointsize 50 -annotate 0 "$1""\n\nDone" \
    -gravity South -pointsize 50 -annotate 0 "SAFE TO POWER OFF" \
    /home/pi/tmp/update.jpg

  # Display the update message on the desktop
  sudo fbi -T 1 -noverbose -a /home/pi/tmp/update.jpg >/dev/null 2>/dev/null
  (sleep 1; sudo killall -9 fbi >/dev/null 2>/dev/null) &  ## kill fbi once it has done its work
  /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &
}

############ Function to Read from Config File ###############

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

##############################################################

reset

printf "\nCommencing update.\n\n"

cd /home/pi

## Check which update to load
GIT_SRC_FILE=".portsdown_gitsrc"
if [ -e ${GIT_SRC_FILE} ]; then
  GIT_SRC=$(</home/pi/${GIT_SRC_FILE})
else
  GIT_SRC="f4dvk"
fi

## If previous version was Dev (davecrump), load production by default
if [ "$GIT_SRC" == "davecrump" ]; then
  GIT_SRC="f4dvk"
fi

if [ "$1" == "-d" ]; then
  echo "Overriding to update to latest development version"
  GIT_SRC="davecrump"
fi

if [ "$GIT_SRC" == "f4dvk" ]; then
  echo "Updating to latest Production Portsdown RPi 4 build";
elif [ "$GIT_SRC" == "davecrump" ]; then
  echo "Updating to latest development Portsdown RPi 4 build";
else
  echo "Updating to latest ${GIT_SRC} development Portsdown RPi 4 build";
fi

printf "Pausing Streamer or TX if running.\n\n"
sudo killall keyedstream >/dev/null 2>/dev/null
sudo killall keyedtx >/dev/null 2>/dev/null
sudo killall ffmpeg >/dev/null 2>/dev/null

# Mise a jour lancee a la main (SSH...) alors que Portsdown tourne sur la
# console : scheduler.sh, source dans le shell de connexion de tty1, relance
# un programme des que celui en cours s'arrete. Le "killall -9 rpidatvgui"
# plus bas sortait avec le code 137 (128 + SIGKILL), que scheduler.sh prenait
# pour une demande de Power Meter : celui-ci restait affiche pendant toute la
# mise a jour, et la copie du nouveau power_meter (binaire en cours
# d'execution) echouait. On arrete donc la console Portsdown avant de copier
# ou compiler quoi que ce soit : getty d'abord (il relance aussitot une
# connexion automatique, donc Portsdown), puis le shell du scheduler avant
# ses programmes. La mise a jour se termine par un redemarrage, qui relance
# tout normalement.
# Lancee depuis le menu (scheduler.sh, code 132), la mise a jour tourne sur
# tty1 et le menu est deja arrete : rien a faire.
if [ "$(ps -o tty= -p $$ | tr -d ' ')" != "tty1" ]; then
  printf "Stopping Portsdown on the console (tty1).\n\n"
  sudo systemctl stop getty@tty1.service >/dev/null 2>/dev/null
  sudo pkill -9 -t tty1 -x bash >/dev/null 2>/dev/null
  sudo pkill -9 -t tty1 >/dev/null 2>/dev/null
  sleep 1
  sudo pkill -9 -t tty1 >/dev/null 2>/dev/null
fi

DisplayUpdateMsg "Step 3 of 10\nSaving Current Config\n\nXXX-------"

PATHSCRIPT="/home/pi/rpidatv/scripts"
PATHUBACKUP="/home/pi/user_backups"
PCONFIGFILE="/home/pi/rpidatv/scripts/portsdown_config.txt"

# Note previous version number
cp -f -r /home/pi/rpidatv/scripts/installed_version.txt /home/pi/prev_installed_version.txt

# Remove previous User Config Backups
rm -rf "$PATHUBACKUP"

# Create a folder for user configs
mkdir "$PATHUBACKUP" >/dev/null 2>/dev/null

# Make a safe copy of portsdown_config.txt and portsdown_presets
cp -f -r "$PATHSCRIPT"/portsdown_config.txt "$PATHUBACKUP"/portsdown_config.txt

# Make a safe copy of portsdown_presets.txt
cp -f -r "$PATHSCRIPT"/portsdown_presets.txt "$PATHUBACKUP"/portsdown_presets.txt

# Make a safe copy of siggencal.txt
cp -f -r /home/pi/rpidatv/src/siggen/siggencal.txt "$PATHUBACKUP"/siggencal.txt

# Make a safe copy of siggenconfig.txt
cp -f -r /home/pi/rpidatv/src/siggen/siggenconfig.txt "$PATHUBACKUP"/siggenconfig.txt

# Make a safe copy of rtl-fm_presets.txt
cp -f -r "$PATHSCRIPT"/rtl-fm_presets.txt "$PATHUBACKUP"/rtl-fm_presets.txt

# Make a safe copy of portsdown_locators.txt
cp -f -r "$PATHSCRIPT"/portsdown_locators.txt "$PATHUBACKUP"/portsdown_locators.txt

# Make a safe copy of rx_presets.txt
cp -f -r "$PATHSCRIPT"/rx_presets.txt "$PATHUBACKUP"/rx_presets.txt

# Make a safe copy of the Stream Presets
cp -f -r "$PATHSCRIPT"/stream_presets.txt "$PATHUBACKUP"/stream_presets.txt

# Make a safe copy of the Jetson config
cp -f -r "$PATHSCRIPT"/jetson_config.txt "$PATHUBACKUP"/jetson_config.txt

# Make a safe copy of the LongMynd config
cp -f -r "$PATHSCRIPT"/longmynd_config.txt "$PATHUBACKUP"/longmynd_config.txt

# Make a safe copy of the Lime Calibration frequency or status
cp -f -r "$PATHSCRIPT"/limecalfreq.txt "$PATHUBACKUP"/limecalfreq.txt

# Make a safe copy of the Band Viewer config
cp -f -r /home/pi/rpidatv/src/bandview/bandview_config.txt "$PATHUBACKUP"/bandview_config.txt

# Make a safe copy of the Airspy Band Viewer config
cp -f -r /home/pi/rpidatv/src/airspyview/airspyview_config.txt "$PATHUBACKUP"/airspyview_config.txt

# Make a safe copy of the RTL-SDR Band Viewer config
cp -f -r /home/pi/rpidatv/src/rtlsdrview/rtlsdrview_config.txt "$PATHUBACKUP"/rtlsdrview_config.txt

# Make a safe copy of the Pluto Band Viewer config
cp -f -r /home/pi/rpidatv/src/plutoview/plutoview_config.txt "$PATHUBACKUP"/plutoview_config.txt

# Make a safe copy of the Pluto Band Viewer Bands config
cp -f -r /home/pi/rpidatv/src/plutoview/plutoview_bands.txt "$PATHUBACKUP"/plutoview_bands.txt

# Make a safe copy of the SDRplay Band Viewer config
cp -f -r /home/pi/rpidatv/src/sdrplayview/sdrplayview_config.txt "$PATHUBACKUP"/sdrplayview_config.txt

# Make a safe copy of the Lime and Pluto NF Meter Configs
cp -f -r /home/pi/rpidatv/src/nf_meter/nf_meter_config.txt "$PATHUBACKUP"/nf_meter_config.txt
cp -f -r /home/pi/rpidatv/src/pluto_nf_meter/pluto_nf_meter_config.txt "$PATHUBACKUP"/pluto_nf_meter_config.txt

# Make a safe copy of the Lime and Pluto Noise Meter Configs
cp -f -r /home/pi/rpidatv/src/noise_meter/noise_meter_config.txt "$PATHUBACKUP"/noise_meter_config.txt
cp -f -r /home/pi/rpidatv/src/pluto_noise_meter/pluto_noise_meter_config.txt "$PATHUBACKUP"/pluto_noise_meter_config.txt

# Make a safe copy of the Meteor Viewer config
cp -f -r /home/pi/rpidatv/src/meteorview/meteorview_config.txt "$PATHUBACKUP"/meteorview_config.txt

# Make a safe copy of the Contest Codes
cp -f -r "$PATHSCRIPT"/portsdown_C_codes.txt "$PATHUBACKUP"/portsdown_C_codes.txt

# Make a safe copy of the User Button scripts
cp -f -r "$PATHSCRIPT"/user_button1.sh "$PATHUBACKUP"/user_button1.sh
cp -f -r "$PATHSCRIPT"/user_button2.sh "$PATHUBACKUP"/user_button2.sh
cp -f -r "$PATHSCRIPT"/user_button3.sh "$PATHUBACKUP"/user_button3.sh
cp -f -r "$PATHSCRIPT"/user_button4.sh "$PATHUBACKUP"/user_button4.sh
cp -f -r "$PATHSCRIPT"/user_button5.sh "$PATHUBACKUP"/user_button5.sh

# Make a safe copy of the transmit start and transmit stop scripts
cp -f -r "$PATHSCRIPT"/TXstartextras.sh "$PATHUBACKUP"/TXstartextras.sh
cp -f -r "$PATHSCRIPT"/TXstopextras.sh "$PATHUBACKUP"/TXstopextras.sh

# Make a safe copy of the user's Test cards
cp -f -r "$PATHSCRIPT"/images "$PATHUBACKUP"/images

# Hotspot Config
cp -f -r "$PATHSCRIPT"/hotspot_config.txt "$PATHUBACKUP"/hotspot_config.txt

# Sarsat
cp -f -r /home/pi/rpidatv/406/decode.txt "$PATHUBACKUP"/decode.txt
cp -f -r /home/pi/rpidatv/406/config.txt "$PATHUBACKUP"/sarsat_config.txt

# Make a safe copy of the HamTV Merger config
cp -f -r "$PATHSCRIPT"/merger_config.txt "$PATHUBACKUP"/merger_config.txt

# Make a safe copy of the potential Fixed IP config
cp -f -r /home/pi/rpidatv/scripts/configs/dhcpcd.conf.prep "$PATHUBACKUP"/dhcpcd.conf.prep

# Make a safe copy of the ISS Tracker config
cp -f -r "$PATHSCRIPT"/tracker_config.txt "$PATHUBACKUP"/tracker_config.txt

# Make a safe copy of the Multi Decoder settings (written by multidec)
if [ -f "$PATHSCRIPT"/multidec_config.txt ]; then
  cp -f "$PATHSCRIPT"/multidec_config.txt "$PATHUBACKUP"/multidec_config.txt
fi

# Make a safe copy of the user's Muntjac cal files
cp -f -r /home/pi/rpidatv/bin/*.mjo "$PATHUBACKUP"/

DisplayUpdateMsg "Step 4 of 10\nUpdating Software Package List\n\nXXXX------"

# Amend the sources.list to legacy
sudo bash -c 'echo -e "deb http://legacy.raspbian.org/raspbian/ buster main contrib non-free rpi" > /etc/apt/sources.list'

# Check for the VLC apt Preferences File.  If not present, write it, and re-install VLC
# Because installed version may not be the right one
cd /home/pi
if [ ! -f  /etc/apt/preferences.d/vlc ]; then
  wget https://github.com/${GIT_SRC}/portsdown4/raw/master/scripts/configs/vlc
  sudo cp vlc /etc/apt/preferences.d/vlc
  sudo apt -y remove vlc*
  sudo apt -y remove libvlc*
  sudo apt -y remove vlc-data

  sudo dpkg --configure -a                            # Make sure that all the packages are properly configured
  sudo apt-get clean                                  # Clean up the old archived packages
  sudo apt-get update --allow-releaseinfo-change      # Update the package list

  # --------- Remove any previous hold on VLC -----------------

  if apt-mark showhold | grep -q 'vlc'; then
    sudo apt-mark unhold vlc
    sudo apt-mark unhold libvlc-bin
    sudo apt-mark unhold libvlc5
    sudo apt-mark unhold libvlccore9
    sudo apt-mark unhold vlc-bin
    sudo apt-mark unhold vlc-data
    sudo apt-mark unhold vlc-plugin-base
    sudo apt-mark unhold vlc-plugin-qt
    sudo apt-mark unhold vlc-plugin-video-output
    sudo apt-mark unhold vlc-l10n
    sudo apt-mark unhold vlc-plugin-notify
    sudo apt-mark unhold vlc-plugin-samba
    sudo apt-mark unhold vlc-plugin-skins2
    sudo apt-mark unhold vlc-plugin-video-splitter
    sudo apt-mark unhold vlc-plugin-visualization
  fi

  sudo apt-get -y dist-upgrade # Upgrade all the installed packages to their latest version

  echo
  echo "Updating VLC"
  echo

  sudo apt-get -y install vlc                         # Reload the correct version

else                                                  # VLC is the correct version, so leave it there

  sudo dpkg --configure -a                            # Make sure that all the packages are properly configured
  sudo apt-get clean                                  # Clean up the old archived packages
  sudo apt-get update --allow-releaseinfo-change      # Update the package list

fi

sudo apt-get -y dist-upgrade # Upgrade all the installed packages to their latest version

echo
echo "Checking for EEPROM Update"
echo

sudo rpi-eeprom-update -a                            # Update will be installed on reboot if required

# --------- Install new packages as Required ---------

DisplayUpdateMsg "Step 5 of 10\nUpdating Software Packages\n\nXXXX------"

# Install libiio and dependencies if required (used for Pluto SigGen)
if [ ! -f  /home/pi/libiio/iio.h ]; then
  echo "Installing libiio and dependencies"
  echo
  sudo apt-get -y install libxml2 libxml2-dev bison flex libcdk5-dev
  sudo apt-get -y install libaio-dev libserialport-dev libxml2-dev libavahi-client-dev
  cd /home/pi
  sudo rm -r libiio
  git clone https://github.com/analogdevicesinc/libiio.git
  cd libiio
  git reset --hard b6028fdeef888ab45f7c1dd6e4ed9480ae4b55e3  # Back to Version 0.25
  cmake -DWITH_EXAMPLES=ON ./
  make -j$MAKE_J all
  sudo make install
  cd /home/pi
else
  echo "Found libiio installed"
  echo
fi

# Install nginx and fastcgi for web access
if [ ! -d  /etc/nginx ]; then
  echo "Installing nginx light web server for web access"
  echo
  sudo apt-get -y install nginx-light                                     # For web access
  sudo apt-get -y install libfcgi-dev                                     # For web control
else
  echo "Found nginx light web server installed"
  echo
fi

# Paquets necessaires (un seul appel a apt ; ceux de 406/install.sh compris)
PACKAGES=(
  libairspy-dev                     # Airspy Bandviewer
  expect                            # unattended installs
  uhubctl                           # SDRPlay USB resets
  libssl-dev                        # libwebsockets
  libiio-dev libzstd-dev            # libiio 202309040
  arp-scan                          # List Network Devices
  cppcheck                          # HamTV Merger Client
  libcurl4-openssl-dev              # cam_ctl
  socat                             # debugging network issues
  lsof                              # debugging web control issues
  python3-dev python3-pip           # ISS tracker
  libhamlib-dev libhamlib++-dev libhamlib-utils   # ISS tracker
  pi-bluetooth bluealsa             # Bluetooth audio
  picotool                          # Flasheur de RP2040
  gcc build-essential sox perl sendemail          # decodeur 406 (406/install.sh)
)
apt_install "${PACKAGES[@]}"

# Modules Python du suivi ISS, seulement s'ils manquent
python3 -c "import ephem, requests" 2>/dev/null || pip3 install ephem requests

sudo adduser pi bluetooth
if ! grep -q noplugin=sap /lib/systemd/system/bluetooth.service; then
  sudo cp /lib/systemd/system/bluetooth.service /lib/systemd/system/bluetooth.service-org
  sudo sed -i 's|^ExecStart=/usr/lib/bluetooth/bluetoothd$|ExecStart=/usr/lib/bluetooth/bluetoothd --noplugin=sap|' /lib/systemd/system/bluetooth.service
fi


# Install libwebsockets if required
if [ ! -d  /home/pi/libwebsockets ]; then
  cd /home/pi
  git clone https://github.com/warmcat/libwebsockets.git
  cd libwebsockets
  cmake ./
  make -j$MAKE_J all
  sudo make install
  sudo ldconfig
  cd /home/pi
fi

# Re-install libwebsockets if failed on April 2026
if grep -q 'ts.tv_sec = target_us / 1000000' /home/pi/libwebsockets/lib/core-net/txpacer.c; then
  cd /home/pi
  sudo rm -rf libwebsockets
  git clone https://github.com/warmcat/libwebsockets.git
  cd libwebsockets
  cmake ./
  make -j$MAKE_J all
  sudo make install
  sudo ldconfig
  cd /home/pi
fi


if ! command -v ffmpeg >/dev/null; then
  sudo apt-get install -y ffmpeg
fi
sudo rm -f /usr/bin/ffmpeg2          # copie qui ne servait qu'au serveur 3LAS (retire)
if [ ! $(find /usr/bin -name aplay2) ]; then
  sudo cp /usr/bin/aplay /usr/bin/aplay2
fi


cd /home/pi

# -----------Update LimeSuite if required -------------

if ! grep -q 9c983d8 /home/pi/LimeSuite/commit_tag.txt; then

  # Remove old LimeSuite
  rm -rf /home/pi/LimeSuite/

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

  # Current LimeSDR Mini V2 Version from LimeSuite 22.09
  wget https://downloads.myriadrf.org/project/limesuite/22.09/LimeSDR-Mini_HW_2.0_r2.2.bit -O \
                 /home/pi/.local/share/LimeSuite/images/22.09/LimeSDR-Mini_HW_2.0_r2.2.bit

  # Check that it was downloaded, if not, go to source and get it
  if [[ "$?" != "0" ]]; then
    rm /home/pi/.local/share/LimeSuite/images/22.09/LimeSDR-Mini_HW_2.0_r2.2.bit
    wget https://github.com/myriadrf/LimeSDR-Mini-v2_GW/raw/main/LimeSDR-Mini_bitstreams/lms7_trx_impl1.bit -O \
      /home/pi/.local/share/LimeSuite/images/22.09/LimeSDR-Mini_HW_2.0_r2.2.bit
  fi
fi

if [ ! -f  /usr/local/bin/dsd ]; then
  sudo apt-get -y install libsndfile1-dev fftw3-dev liblapack-dev portaudio19-dev

  git clone https://github.com/szechyjs/mbelib.git
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

  git clone https://github.com/f4exb/dsdcc.git
  cd dsdcc
  mkdir build && cd build
  cmake -DUSE_MBELIB=ON ..
  make -j$MAKE_J
  sudo make install
  cd /home/pi
  sudo rm -r dsdcc

  git clone https://github.com/f4dvk/dsd.git
  cd dsd
  mkdir build && cd build
  cmake ..
  make -j$MAKE_J
  sudo make install
  cd /home/pi
  sudo rm -r dsd
fi

# ---------- Update rpidatv -----------

DisplayUpdateMsg "Step 6 of 10\nDownloading Portsdown SW\n\nXXXXX-----"

echo
echo "-------------------------------------------------------"
echo "----- Updating the Portsdown Touchscreen Software -----"
echo "-------------------------------------------------------"

cd /home/pi

# Delete previous update folder if downloaded in error
rm -rf portsdown4-master >/dev/null 2>/dev/null

# Download selected source of rpidatv
wget https://github.com/${GIT_SRC}/portsdown4/archive/master.zip -O master.zip

# Unzip and overwrite where we need to
unzip -o master.zip
cp -f -r portsdown4-master/bin rpidatv
cp -f -r portsdown4-master/scripts rpidatv
cp -f -r -p portsdown4-master/src rpidatv
cp -f -r portsdown4-master/406 rpidatv
cp -f -r portsdown4-master/server rpidatv
rm -f rpidatv/video/*.jpg
cp -f -r portsdown4-master/video rpidatv
cp -f -r portsdown4-master/version_history.txt rpidatv/version_history.txt
cp -f portsdown4-master/add_langstone.sh rpidatv/add_langstone.sh
cp -f portsdown4-master/add_langstone2.sh rpidatv/add_langstone2.sh
cp -f portsdown4-master/add_ryde.sh rpidatv/add_ryde.sh
cp -f portsdown4-master/update_ryde.sh rpidatv/update_ryde.sh

# Copy the recently added images into the user's back-up image folder
cp portsdown4-master/scripts/images/web_not_enabled.png "$PATHUBACKUP"/images/web_not_enabled.png
cp portsdown4-master/scripts/images/RX_overlay.png "$PATHUBACKUP"/images/RX_overlay.png
cp portsdown4-master/scripts/images/tcf1080.jpg "$PATHUBACKUP"/images/tcf1080.jpg

rm master.zip
rm -rf portsdown4-master
cd /home/pi

DisplayUpdateMsg "Step 7 of 10\nCompiling Portsdown SW\n\nXXXXXX----"

sudo killall -9 rpidatvgui

echo
echo "-----------------------------------------------"
echo "------- Update RTL-SDR Drivers and Apps -------"
echo "-----------------------------------------------"
# Recompile seulement si le depot osmocom a change depuis la derniere fois
cd /home/pi
RTL_REMOTE=$(git ls-remote https://github.com/osmocom/rtl-sdr.git HEAD 2>/dev/null | cut -f1)
if [ -n "$RTL_REMOTE" ] && [ -f /usr/local/lib/librtlsdr.so ] \
  && [ "$(git -C /home/pi/rtl-sdr rev-parse HEAD 2>/dev/null)" == "$RTL_REMOTE" ]; then
  echo "rtl-sdr already up to date"
else
  sudo rm -rf rtl-sdr
  git clone https://github.com/osmocom/rtl-sdr.git

  # Compile and install rtl-sdr
  cd rtl-sdr/ && mkdir build && cd build
  cmake ../ -DINSTALL_UDEV_RULES=ON
  make -j$MAKE_J && sudo make install && sudo ldconfig
  cd /home/pi
fi

if [ ! -f  /usr/local/lib/libSoapySDR.so ]; then
  # Install SoapySDR
  git clone https://github.com/pothosware/SoapySDR.git
  cd SoapySDR
  mkdir build && cd build
  cmake ..
  make -j$MAKE_J
  sudo make install
  cd /home/pi

  # Install SoapyPlutoSDR
  git clone https://github.com/pothosware/SoapyPlutoSDR.git
  cd SoapyPlutoSDR
  mkdir build && cd build
  cmake ..
  make -j$MAKE_J
  sudo make install
  cd /home/pi

  sudo rm -r SoapySDR
  sudo rm -r SoapyPlutoSDR
else
  echo "Found SoapySDR installed"
  echo
fi

if [ ! -f /usr/bin/csdr ]; then
  # Install csdr
  git clone https://github.com/ha7ilm/csdr.git
  cd csdr
  make -j$MAKE_J
  sudo make install
  cd /home/pi

  sudo rm -r csdr
else
  echo "Found csdr installed"
  echo
fi

echo
echo "---------------------------------------------"
echo "----- Compiling the Portsdown programs ------"
echo "---------------------------------------------"
# Programmes independants compiles en parallele, selon les coeurs et la memoire
# disponible, et seulement si leurs sources ont change (scripts/build_jobs.sh) ;
# journaux dans /home/pi/build_logs
source /home/pi/rpidatv/scripts/build_jobs.sh
build_portsdown_programs
cd /home/pi

# Decodeur multiple : decodeurs externes (recompiles seulement quand leur
# version change) et fichiers de la carte web (telecharges une seule fois)
echo "----- Multi Decoder : external decoders and map -----"
/home/pi/rpidatv/src/multidec/install_decoders.sh
/home/pi/rpidatv/src/multidec/install_map.sh
cd /home/pi

DisplayUpdateMsg "Step 8 of 10\nRestoring Config\n\nXXXXXXXX--"

# Restore portsdown_config.txt and portsdown_presets.txt
cp -f -r "$PATHUBACKUP"/portsdown_config.txt "$PATHSCRIPT"/portsdown_config.txt
cp -f -r "$PATHUBACKUP"/portsdown_presets.txt "$PATHSCRIPT"/portsdown_presets.txt

# Restore the user's original siggencal.txt (but not yet as it keeps changing)
#cp -f -r "$PATHUBACKUP"/siggencal.txt /home/pi/rpidatv/src/siggen/siggencal.txt

# Restore the user's original siggenconfig.txt (but not yet as it keeps changing)
#cp -f -r "$PATHUBACKUP"/siggenconfig.txt /home/pi/rpidatv/src/siggen/siggenconfig.txt

# Restore the user's rtl-fm_presets.txt
cp -f -r "$PATHUBACKUP"/rtl-fm_presets.txt "$PATHSCRIPT"/rtl-fm_presets.txt

# Restore the user's original portsdown_locators.txt
cp -f -r "$PATHUBACKUP"/portsdown_locators.txt "$PATHSCRIPT"/portsdown_locators.txt

# Restore the user's original rx_presets.txt
if grep -q upsample "$PATHUBACKUP"/rx_presets.txt; then
  cp -f -r "$PATHUBACKUP"/rx_presets.txt "$PATHSCRIPT"/rx_presets.txt
fi

# Restore the user's original stream presets
cp -f -r "$PATHUBACKUP"/stream_presets.txt "$PATHSCRIPT"/stream_presets.txt

# Restore the user's original Jetson configuration
cp -f -r "$PATHUBACKUP"/jetson_config.txt "$PATHSCRIPT"/jetson_config.txt

# Restore the user's original LongMynd config
cp -f -r "$PATHUBACKUP"/longmynd_config.txt "$PATHSCRIPT"/longmynd_config.txt

# Restore the user's original Lime Calibration frequency or status
cp -f -r "$PATHUBACKUP"/limecalfreq.txt "$PATHSCRIPT"/limecalfreq.txt

# Restore the user's original Band Viewer config
cp -f -r "$PATHUBACKUP"/bandview_config.txt /home/pi/rpidatv/src/bandview/bandview_config.txt

# Restore the user's original Airspy Band Viewer config
cp -f -r "$PATHUBACKUP"/airspyview_config.txt /home/pi/rpidatv/src/airspyview/airspyview_config.txt

# Restore the user's original RTL-SDR Band Viewer config
cp -f -r "$PATHUBACKUP"/rtlsdrview_config.txt /home/pi/rpidatv/src/rtlsdrview/rtlsdrview_config.txt

# Restore the user's original Pluto Band Viewer config
cp -f -r "$PATHUBACKUP"/plutoview_config.txt /home/pi/rpidatv/src/plutoview/plutoview_config.txt

if [ -f  "$PATHUBACKUP"/plutoview_bands.txt ]; then
  # Restore the user's original Pluto Band Viewer Bands
  cp -f -r "$PATHUBACKUP"/plutoview_bands.txt /home/pi/rpidatv/src/plutoview/plutoview_bands.txt
fi

# Restore the user's original SDRplay Band Viewer config
cp -f -r "$PATHUBACKUP"/sdrplayview_config.txt /home/pi/rpidatv/src/sdrplayview/sdrplayview_config.txt

# Restore the user's original Lime and Pluto NF Meter Configs
cp -f -r "$PATHUBACKUP"/nf_meter_config.txt /home/pi/rpidatv/src/nf_meter/nf_meter_config.txt
cp -f -r "$PATHUBACKUP"/pluto_nf_meter_config.txt /home/pi/rpidatv/src/pluto_nf_meter/pluto_nf_meter_config.txt

# Restore the user's original Lime and Pluto Noise Meter Configs
cp -f -r "$PATHUBACKUP"/noise_meter_config.txt /home/pi/rpidatv/src/noise_meter/noise_meter_config.txt
cp -f -r "$PATHUBACKUP"/pluto_noise_meter_config.txt /home/pi/rpidatv/src/pluto_noise_meter/pluto_noise_meter_config.txt

# Restore the user's original Meteor Viewer config
cp -f -r "$PATHUBACKUP"/meteorview_config.txt /home/pi/rpidatv/src/meteorview/meteorview_config.txt

# Restore the user's original Contest Codes
cp -f -r "$PATHUBACKUP"/portsdown_C_codes.txt "$PATHSCRIPT"/portsdown_C_codes.txt

# Restore the user's original User Button scripts
cp -f -r "$PATHUBACKUP"/user_button1.sh "$PATHSCRIPT"/user_button1.sh
cp -f -r "$PATHUBACKUP"/user_button2.sh "$PATHSCRIPT"/user_button2.sh
cp -f -r "$PATHUBACKUP"/user_button3.sh "$PATHSCRIPT"/user_button3.sh
cp -f -r "$PATHUBACKUP"/user_button4.sh "$PATHSCRIPT"/user_button4.sh
cp -f -r "$PATHUBACKUP"/user_button5.sh "$PATHSCRIPT"/user_button5.sh

# Restore the user's original transmit start and transmit stop scripts
cp -f -r "$PATHUBACKUP"/TXstartextras.sh "$PATHSCRIPT"/TXstartextras.sh
cp -f -r "$PATHUBACKUP"/TXstopextras.sh "$PATHSCRIPT"/TXstopextras.sh

# Restore hotspot_config
cp -f -r "$PATHUBACKUP"/hotspot_config.txt "$PATHSCRIPT"/hotspot_config.txt

# sarsat
cp -f -r "$PATHUBACKUP"/decode.txt /home/pi/rpidatv/406/decode.txt
cp -f -r "$PATHUBACKUP"/sarsat_config.txt /home/pi/rpidatv/406/config.txt

# Restore the user's original test cards if required
if test -f "$PATHUBACKUP"/images/tccw.jpg ; then     # Test card functionality included pre-update
  rm -rf "$PATHSCRIPT"/images
  cp -f -r "$PATHUBACKUP"/images "$PATHSCRIPT"
fi

# Restore the user's original HamTV Merger config
cp -f -r "$PATHUBACKUP"/merger_config.txt "$PATHSCRIPT"/merger_config.txt

# Restore the user's original ISS Tracker config
cp -f -r "$PATHUBACKUP"/tracker_config.txt "$PATHSCRIPT"/tracker_config.txt

# Restore the user's Multi Decoder settings
if [ -f "$PATHUBACKUP"/multidec_config.txt ]; then
  cp -f "$PATHUBACKUP"/multidec_config.txt "$PATHSCRIPT"/multidec_config.txt
fi

# Restore the user's original Muntjac cal files
cp -f -r "$PATHUBACKUP"/*.mjo /home/pi/rpidatv/bin/

# Restore the user's original potential Fixed IP config
cp -f -r "$PATHUBACKUP"/dhcpcd.conf.prep /home/pi/rpidatv/scripts/configs/dhcpcd.conf.prep

# Add Mic Gain parameter to config file if not included
if ! grep -q micgain "$PATHSCRIPT"/portsdown_config.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/portsdown_config.txt
  # Add the new entry and a new line
  echo "micgain=26" >> "$PATHSCRIPT"/portsdown_config.txt
fi

# Add new parameters to config file if not included  202101090
if ! grep -q udpoutport "$PATHSCRIPT"/portsdown_config.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/portsdown_config.txt
  # Add the new entry and a new line
  echo "udpoutport=10000" >> "$PATHSCRIPT"/portsdown_config.txt
  echo "udpinport=10000" >> "$PATHSCRIPT"/portsdown_config.txt
  echo "guard=32" >> "$PATHSCRIPT"/portsdown_config.txt
fi

# Add another new parameter to config file if not included  202101180
if ! grep -q qam= "$PATHSCRIPT"/portsdown_config.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/portsdown_config.txt
  # Add the new entry and a new line
  echo "qam=qpsk" >> "$PATHSCRIPT"/portsdown_config.txt
fi

# Add new receiver parameters to longmynd_config.txt if not included
if ! grep -q tstimeout "$PATHSCRIPT"/longmynd_config.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/longmynd_config.txt
  # Add the new entry and a new line
  echo "tstimeout=5000" >> "$PATHSCRIPT"/longmynd_config.txt
  echo "tstimeout1=10000" >> "$PATHSCRIPT"/longmynd_config.txt
  echo "scanwidth=50" >> "$PATHSCRIPT"/longmynd_config.txt
  echo "scanwidth1=50" >> "$PATHSCRIPT"/longmynd_config.txt
  echo "chan=0" >> "$PATHSCRIPT"/longmynd_config.txt
  echo "chan1=0" >> "$PATHSCRIPT"/longmynd_config.txt
  echo "rxmod=dvbs" >> "$PATHSCRIPT"/longmynd_config.txt
fi

# Add adf4153 reference freq to siggenconfig.txt if not included
if ! grep -q adf4153ref /home/pi/rpidatv/src/siggen/siggenconfig.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' /home/pi/rpidatv/src/siggen/siggenconfig.txt
  # Add the new entry and a new line
  echo "adf4153ref=20000000" >> /home/pi/rpidatv/src/siggen/siggenconfig.txt
fi

# Add new slo and adf4153 parameters to siggencal.txt if not included
if ! grep -q slopoints /home/pi/rpidatv/src/siggen/siggencal.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' /home/pi/rpidatv/src/siggen/siggencal.txt
  # Add the new entries and a new line
  echo "slopoints=2" >> /home/pi/rpidatv/src/siggen/siggencal.txt
  echo "slofreq1=10000000000" >> /home/pi/rpidatv/src/siggen/siggencal.txt
  echo "slolev1=140" >> /home/pi/rpidatv/src/siggen/siggencal.txt
  echo "slofreq2=14000000000" >> /home/pi/rpidatv/src/siggen/siggencal.txt
  echo "slolev1=140" >> /home/pi/rpidatv/src/siggen/siggencal.txt
  echo "adf4153points=2" >> /home/pi/rpidatv/src/siggen/siggencal.txt
  echo "adf4153freq1=500000000" >> /home/pi/rpidatv/src/siggen/siggencal.txt
  echo "adf4153lev1=0" >> /home/pi/rpidatv/src/siggen/siggencal.txt
  echo "adf4153freq2=4000000000" >> /home/pi/rpidatv/src/siggen/siggencal.txt
  echo "adf4153lev2=0" >> /home/pi/rpidatv/src/siggen/siggencal.txt
fi

# Add ad9850 reference freq to siggenconfig.txt if not included (202208240)
if ! grep -q ad9850ref /home/pi/rpidatv/src/siggen/siggenconfig.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' /home/pi/rpidatv/src/siggen/siggenconfig.txt
  # Add the new entry and a new line
  echo "ad9850ref=120000000" >> /home/pi/rpidatv/src/siggen/siggenconfig.txt
fi

# Add LimeRFE controls to config file if not included  202107010
if ! grep -q limerfeport= "$PATHSCRIPT"/portsdown_config.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/portsdown_config.txt
  # Add the new entry and a new line
  echo "limerfeport=txrx" >> "$PATHSCRIPT"/portsdown_config.txt
  echo "limerferxatt=0" >> "$PATHSCRIPT"/portsdown_config.txt
fi

# Add PiCam and Audio Gain controls to config file if not included  202109010
if ! grep -q picam= "$PATHSCRIPT"/portsdown_config.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/portsdown_config.txt
  # Add the new entry and a new line
  echo "picam=normal" >> "$PATHSCRIPT"/portsdown_config.txt
  echo "vlcvolume=256" >> "$PATHSCRIPT"/portsdown_config.txt
fi

# Add Web Control setting to config file if not included  202203010
if ! grep -q webcontrol= "$PATHSCRIPT"/portsdown_config.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/portsdown_config.txt
  # Add the new entry and a new line
  echo "webcontrol=enabled" >> "$PATHSCRIPT"/portsdown_config.txt
fi

# Add langstone setting to config file if not included  202203070
if ! grep -q langstone= "$PATHSCRIPT"/portsdown_config.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/portsdown_config.txt
  # Add the new entry and a new line
  if [ -d  /home/pi/Langstone ]; then
    # Langstone V1 already installed
    echo "langstone=v1pluto" >> "$PATHSCRIPT"/portsdown_config.txt
  else
    echo "langstone=none" >> "$PATHSCRIPT"/portsdown_config.txt
  fi
fi

# Add gpio setting to config file if not included
if ! grep -q gpio= "$PATHSCRIPT"/portsdown_config.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/portsdown_config.txt
  # Add the new entry and a new line
  echo "gpio=on" >> "$PATHSCRIPT"/portsdown_config.txt
fi

# Add New presets and LimeRFE controls to presets file if not included  202107010
if ! grep -q d0label= "$PATHSCRIPT"/portsdown_presets.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/portsdown_presets.txt
  # Add the new entries to the end
  cat "$PATHSCRIPT"/configs/add_portsdown_presets.txt >> "$PATHSCRIPT"/portsdown_presets.txt
fi

# Add New RX LO presets to RX presets file if not included  202107010
if ! grep -q t8lo= "$PATHSCRIPT"/rx_presets.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/rx_presets.txt
  # Add the new entries to the end
  cat "$PATHSCRIPT"/configs/add_rx_presets.txt >> "$PATHSCRIPT"/rx_presets.txt
fi

# Add SSTV frequency to RX presets file if not included  202610180
if ! grep -q sstv_frequency= "$PATHSCRIPT"/rx_presets.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/rx_presets.txt
  # Add the new entry and a new line
  echo "sstv_frequency=145.9" >> "$PATHSCRIPT"/rx_presets.txt
fi

# Add SSTV SDR selection to RX presets file if not included  202610181
if ! grep -q sstv_sdr= "$PATHSCRIPT"/rx_presets.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/rx_presets.txt
  # Add the new entry and a new line
  echo "sstv_sdr=RTLSDR" >> "$PATHSCRIPT"/rx_presets.txt
fi

# Write new factory Contest Numbers File if required  202107010
if ! grep -q site1d0numbers= "$PATHSCRIPT"/portsdown_C_codes.txt; then
  # File needs updating
  cp "$PATHSCRIPT"/configs/portsdown_C_codes.txt.factory "$PATHSCRIPT"/portsdown_C_codes.txt
fi

# Write new factory Contest Numbers File if required  202406160
if ! grep -q site2d0numbers= "$PATHSCRIPT"/portsdown_C_codes.txt; then
  # File needs updating
  cp "$PATHSCRIPT"/configs/portsdown_C_codes.txt.factory "$PATHSCRIPT"/portsdown_C_codes.txt
fi

# Write new factory Contest Numbers File if required  202406160
if ! grep -q site2t8numbers= "$PATHSCRIPT"/portsdown_C_codes.txt; then
  # File needs updating
  cp "$PATHSCRIPT"/configs/portsdown_C_codes.txt.factory "$PATHSCRIPT"/portsdown_C_codes.txt
fi

# Add Lime upsample setting to config file if not included  202401***
if ! grep -q upsample= "$PATHSCRIPT"/portsdown_config.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/portsdown_config.txt
  # Add the new entry and a new line
  echo "upsample=2" >> "$PATHSCRIPT"/portsdown_config.txt
fi

# Add Pluto Band Viewer LO setting to config file if not included  202402060
if ! grep -q premixlolow= /home/pi/rpidatv/src/plutoview/plutoview_bands.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' /home/pi/rpidatv/src/plutoview/plutoview_bands.txt
  # Add the new entry and a new line
  echo "premixlolow=0.0" >> /home/pi/rpidatv/src/plutoview/plutoview_bands.txt
  echo "premixlohi=0.0" >> /home/pi/rpidatv/src/plutoview/plutoview_bands.txt
fi

# Add time overlay setting to config file if not included  202406160
if ! grep -q timeoverlay= "$PATHSCRIPT"/portsdown_config.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/portsdown_config.txt
  # Add the new entry and a new line
  echo "timeoverlay=off" >> "$PATHSCRIPT"/portsdown_config.txt
fi

if ! grep -q rtsp_ip= "$PATHSCRIPT"/portsdown_config.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/portsdown_config.txt
  # Add the new entry and a new line
  echo "rtsp_ip=192.168.0.26" >> "$PATHSCRIPT"/portsdown_config.txt
  echo "rtsp_port=8554" >> "$PATHSCRIPT"/portsdown_config.txt
  echo "rtsp_usr=admin" >> "$PATHSCRIPT"/portsdown_config.txt
  echo "rtsp_pwd=admin" >> "$PATHSCRIPT"/portsdown_config.txt
fi

# Add Muntjac entries to config file if not included 202503310
if ! grep -q muntjacgain= "$PATHSCRIPT"/portsdown_config.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/portsdown_config.txt
  # Add the new entry and a new line
  echo "muntjacgain=15" >> "$PATHSCRIPT"/portsdown_config.txt
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/portsdown_presets.txt
  # Add the new entry and a new line
  echo "d0muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
  echo "d1muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
  echo "d2muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
  echo "d3muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
  echo "d4muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
  echo "d5muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
  echo "d6muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
  echo "t0muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
  echo "t1muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
  echo "t2muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
  echo "t3muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
  echo "t4muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
  echo "t5muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
  echo "t6muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
  echo "t7muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
  echo "t8muntjacgain=10" >> "$PATHSCRIPT"/portsdown_presets.txt
fi

# Correct HamTV merger test port 202510030
sed -i -e 's/testport=6789/testport=9978/g' /home/pi/rpidatv/scripts/merger_config.txt

# Add HamTV Merger LNB Voltage parameter to config file if required 202510030
if ! grep -q lnbvolts= "$PATHSCRIPT"/merger_config.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/merger_config.txt
  # Add the new entry and a new line
  echo "lnbvolts=off" >> "$PATHSCRIPT"/merger_config.txt
fi

# Son du Pi pour la page web : server/stream.sh (carte son virtuelle ->
# md_audio, /touchscreen/audio). L'ancien serveur 3LAS est retire, avec ce qui
# ne servait qu'a lui : nodejs/npm (et leurs paquets node-*), modules et cache
# npm, ~/.asoundrc (definitions Loopback de ffmpeg2 ; la carte son est dans
# /etc/asound.conf)
chmod ug+x /home/pi/rpidatv/server/stream.sh
rm -rf /home/pi/rpidatv/server/node_modules /home/pi/rpidatv/server/package-lock.json /home/pi/.npm
if dpkg -s nodejs >/dev/null 2>&1 || dpkg -s npm >/dev/null 2>&1; then
  sudo apt-get -y purge nodejs npm nodejs-doc libnode64 libnode-dev gyp libjs-inherits libjs-is-typedarray
fi
if [ -f /home/pi/.asoundrc ] && grep -q plug_Loopback_1_2 /home/pi/.asoundrc && ! grep -q "pcm.!default" /home/pi/.asoundrc; then
  rm -f /home/pi/.asoundrc
fi

if ! grep -q snd-aloop /etc/modules; then
  sudo sed -i '$ s/$/\nsnd-aloop/' /etc/modules
fi

# Add libreSDR IP setting to config file if not included  202406160
if ! grep -q libreip= "$PATHSCRIPT"/portsdown_config.txt; then
  # File needs updating
  # Delete any blank lines first
  sed -i -e '/^$/d' "$PATHSCRIPT"/portsdown_config.txt
  # Add the new entry and a new line
  echo "libreip=dhcp" >> "$PATHSCRIPT"/portsdown_config.txt
fi

# Add new stop alias if required  202311xxx
if ! grep -q rpidatv/scripts/utils/stop.sh /home/pi/.bash_aliases; then
  # File needs updating
  echo "alias stop='/home/pi/rpidatv/scripts/utils/stop.sh'"  >> /home/pi/.bash_aliases
fi

# Configure the nginx web server
sudo systemctl stop nginx
rm -rf /home/pi/webroot
cp -r /home/pi/rpidatv/scripts/configs/webroot /home/pi/webroot
sudo cp /home/pi/rpidatv/scripts/configs/nginx.conf /etc/nginx/nginx.conf
sudo systemctl start nginx

# Create a directory for IQ files if required
if  [ ! -d /home/pi/iqfiles ]; then
  mkdir /home/pi/iqfiles
fi

sleep 1

sudo sed -i 's/^#host-name=foo.*/host-name=rpidatv4/' /etc/avahi/avahi-daemon.conf

DisplayUpdateMsg "Step 9 of 10\nInstall Or Update Langstone\n\nXXXXXXXXX-"

echo
echo "------------------------------------------"
echo "------------ Update Langstone ------------"
echo "------------------------------------------"
if grep -q langstone=none "$PATHSCRIPT"/portsdown_config.txt; then
  /home/pi/rpidatv/add_langstone2.sh
else
  /home/pi/rpidatv/scripts/update_langstone2.sh
fi

DisplayUpdateMsg "Step 9 of 10\nFinishing Off\n\nXXXXXXXXX-"

# Update the version number

cp /home/pi/prev_installed_version.txt /home/pi/rpidatv/scripts/prev_installed_version.txt
rm /home/pi/prev_installed_version.txt
rm /home/pi/rpidatv/scripts/installed_version.txt
cp /home/pi/rpidatv/scripts/latest_version.txt /home/pi/rpidatv/scripts/installed_version.txt

# Save (overwrite) the git source location used
echo "${GIT_SRC}" > /home/pi/${GIT_SRC_FILE}

# Installation décodage 406
cd /home/pi/rpidatv/406
./install.sh

# installation de hostapd et dnsmasq
dpkg -l | grep hostapd >/dev/null 2>/dev/null
if [ $? != 0 ]; then
  sudo apt-get -f -y install hostapd
  sudo systemctl disable hostapd
  sudo service hostapd stop
fi

dpkg -l | grep dnsmasq >/dev/null 2>/dev/null
if [ $? != 0 ]; then
  sudo apt-get -f -y install dnsmasq
  sudo systemctl disable dnsmasq
  sudo service dnsmasq stop
fi

# Réduction temps démarrage sans ethernet
sudo sed -i 's/^TimeoutStartSec.*/TimeoutStartSec=5/' /etc/systemd/system/network-online.target.wants/networking.service
sudo sed -i 's/^#timeout.*/timeout 8;/' /etc/dhcp/dhclient.conf
sudo sed -i 's/^#retry.*/retry 20;/' /etc/dhcp/dhclient.conf

# Reboot
DisplayRebootMsg "Step 10 of 10\nRebooting\n\nUpdate Complete"
/home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &
printf "\nRebooting\n"

sleep 5  ## Allow rebooting message to be displayed on web view
# Turn off swap to prevent reboot hang
sudo swapoff -a

sudo shutdown -r now  # Seems to be more reliable than reboot

exit
