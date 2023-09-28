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

DisplayUpdateMsg "Step 3 of 10\nSaving Current Config\n\nXXX-------"
/home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &

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

# Make a safe copy of the SDRplay Band Viewer config
cp -f -r /home/pi/rpidatv/src/sdrplayview/sdrplayview_config.txt "$PATHUBACKUP"/sdrplayview_config.txt

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

DisplayUpdateMsg "Step 4 of 10\nUpdating Software Package List\n\nXXXX------"
/home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &

# Download and install the VLC apt Preferences File 202212010
cd /home/pi
if [ ! -f  /etc/apt/preferences.d/vlc ]; then
  wget https://github.com/${GIT_SRC}/portsdown4/raw/master/scripts/configs/vlc
  sudo cp vlc /etc/apt/preferences.d/vlc
fi

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

DisplayUpdateMsg "Step 5 of 10\nUpdating Software Packages\n\nXXXX------"
/home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &

# --------- Update Packages ------

sudo apt-get -y dist-upgrade # Upgrade all the installed packages to their latest version

# --------- Install new packages as Required ---------

sudo apt-get -y install vlc                       # Removed earlier

echo

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
  make all
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

sudo apt-get -y install libairspy-dev                                   # For Airspy Bandviewer
sudo apt-get -y install expect                                          # For unattended installs
sudo apt-get -y install uhubctl                                         # For SDRPlay USB resets
sudo apt-get -y install libssl-dev                                      # For libwebsockets
sudo apt-get -y install libzstd-dev                                     # For libiio 202309040

# Install libwebsockets if required
if [ ! -d  /home/pi/libwebsockets ]; then
  cd /home/pi
  git clone https://github.com/warmcat/libwebsockets.git
  cd libwebsockets
  cmake ./
  make all
  sudo make install
  sudo ldconfig
  cd /home/pi
fi

# Install the sdrplay drivers if required
if [ ! -f  /usr/local/include/sdrplay_api.h ]; then
  cd /home/pi/rpidatv/src/meteorview

  # Download api if required
  if [ ! -f  SDRplay_RSP_API-ARM-3.09.1.run ]; then
    wget https://www.sdrplay.com/software/SDRplay_RSP_API-ARM-3.09.1.run
  fi
  chmod +x SDRplay_RSP_API-ARM-3.09.1.run

  # Create file to trigger install on next reboot
  touch /home/pi/rpidatv/.post-install_actions
  cd /home/pi
fi

sudo apt-get install -y nodejs npm         # streaming audio

if [ ! $(find /usr/bin -name ffmpeg2) ]; then
  sudo apt-get install -y ffmpeg
  sudo cp /usr/bin/ffmpeg /usr/bin/ffmpeg2
fi
if [ ! $(find /usr/bin -name aplay2) ]; then
  sudo cp /usr/bin/aplay /usr/bin/aplay2
fi

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
  make
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


# ---------- Update rpidatv -----------

DisplayUpdateMsg "Step 6 of 10\nDownloading Portsdown SW\n\nXXXXX-----"
/home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &

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
cp -f -r portsdown4-master/src rpidatv
cp -f -r portsdown4-master/406 rpidatv
cp -f -r portsdown4-master/server rpidatv
rm -f rpidatv/video/*.jpg
cp -f -r portsdown4-master/video rpidatv
cp -f -r portsdown4-master/version_history.txt rpidatv/version_history.txt
cp -f portsdown4-master/add_langstone.sh rpidatv/add_langstone.sh
cp -f portsdown4-master/add_langstone2.sh rpidatv/add_langstone2.sh

# Copy the recently added images into the user's back-up image folder
cp portsdown4-master/scripts/images/web_not_enabled.png "$PATHUBACKUP"/images/web_not_enabled.png
cp portsdown4-master/scripts/images/RX_overlay.png "$PATHUBACKUP"/images/RX_overlay.png

rm master.zip
rm -rf portsdown4-master
cd /home/pi

DisplayUpdateMsg "Step 7 of 10\nCompiling Portsdown SW\n\nXXXXXX----"
/home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &

# Compile rpidatv gui
sudo killall -9 rpidatvgui
echo "Installing rpidatvgui"
cd /home/pi/rpidatv/src/gui
#make clean
make
sudo make install

echo
echo "-----------------------------------------------"
echo "------- Update RTL-SDR Drivers and Apps -------"
echo "-----------------------------------------------"
cd /home/pi
sudo rm -r rtl-sdr
wget https://github.com/f4dvk/rtl-sdr/archive/master.zip
unzip master.zip
mv rtl-sdr-master rtl-sdr
rm master.zip

# Compile and install rtl-sdr
cd rtl-sdr/ && mkdir build && cd build
cmake ../ -DINSTALL_UDEV_RULES=ON
make && sudo make install && sudo ldconfig

# Update limesdr_toolbox
echo "Updating limesdr_toolbox"

cd /home/pi/rpidatv/src/limesdr_toolbox

# Install sub project dvb modulation
# Download and overwrite
wget https://github.com/F5OEO/libdvbmod/archive/master.zip -O master.zip
unzip -o master.zip
rm -rf libdvbmod
cp -f -r libdvbmod-master libdvbmod
rm master.zip
rm -rf libdvbmod-master

# Make libdvbmod
cd libdvbmod/libdvbmod
make
cd ../DvbTsToIQ/
make
cp dvb2iq /home/pi/rpidatv/bin/

#Make limesdr_toolbox
cd /home/pi/rpidatv/src/limesdr_toolbox/
make
cp limesdr_send /home/pi/rpidatv/bin/
cp limesdr_dump /home/pi/rpidatv/bin/
cp limesdr_stopchannel /home/pi/rpidatv/bin/
cp limesdr_forward /home/pi/rpidatv/bin/
make dvb
cp limesdr_dvb /home/pi/rpidatv/bin/
cd /home/pi

echo
echo "--------------------------------"
echo "----- Updating dvb_t_stack -----"
echo "--------------------------------"
cd /home/pi/rpidatv/src/dvb_t_stack/Release
make clean
make
cp dvb_t_stack /home/pi/rpidatv/bin/dvb_t_stack

# Install the DATV Express firmware files
cd /home/pi/rpidatv/src/dvb_t_stack
sudo cp datvexpress16.ihx /lib/firmware/datvexpress/datvexpress16.ihx
sudo cp datvexpressraw16.rbf /lib/firmware/datvexpress/datvexpressraw16.rbf
cd /home/pi

echo
echo "-------------------------------------"
echo "----- Updating the H264 Encoder -----"
echo "-------------------------------------"
cd /home/pi/avc2ts
rm avc2ts.cpp

# Download the previously selected version of avc2ts.cpp for Portsdown 4
wget https://github.com/BritishAmateurTelevisionClub/avc2ts/raw/portsdown4/avc2ts.cpp

# Make avc2ts with new source
make
cp avc2ts ../rpidatv/bin/
cd /home/pi

echo
echo "------------------------------------------"
echo "----- Updating the LongMynd Receiver -----"
echo "------------------------------------------"
cd /home/pi
rm -rf longmynd
cp -r /home/pi/rpidatv/src/longmynd/ /home/pi/
cd longmynd
make
cd /home/pi

echo
echo "--------------------------------------------"
echo "------ Installing the LeanDVB Receiver -----"
echo "--------------------------------------------"
#install leandvb DVB-S2
cd /home/pi/rpidatv/src/
sudo rm -fr leansdr
wget https://github.com/f4dvk/leansdr/archive/master.zip
unzip master.zip
mv leansdr-master leansdr
rm master.zip
cd leansdr/src/apps
make
cp leandvb ../../../../bin/

cd /home/pi/rpidatv/src/fake_read
make
cp fake_read ../../bin/
cd /home/pi

echo
echo "------------------------------------------"
echo "----- Compiling the Signal Generator -----"
echo "------------------------------------------"
cd /home/pi/rpidatv/src/siggen
touch siggentouch4.c            # Force recompilation for LimeSuite update
make
sudo make install
cd /home/pi

echo
echo "----------------------------------------"
echo "----- Compiling the ADF4351 driver -----"
echo "----------------------------------------"
cd /home/pi/rpidatv/src/adf4351
make
cp adf4351 ../../bin/
cd /home/pi

# Compile Band Viewer
echo
echo "---------------------------------"
echo "----- Compiling Band Viewer -----"
echo "---------------------------------"
cd /home/pi/rpidatv/src/bandview
make
cp bandview ../../bin/
# Copy the fftw wisdom file to home so that there is no start-up delay
# This file works for both BandViewer and NF Meter
cp .fftwf_wisdom /home/pi/.fftwf_wisdom
cd /home/pi

# Compile Airspy Band Viewer
echo
echo "----------------------------------------"
echo "----- Compiling Airspy Band Viewer -----"
echo "----------------------------------------"
cd /home/pi/rpidatv/src/airspyview
make
cp airspyview ../../bin/
cd /home/pi

# Compile RTL-SDR Band Viewer
echo
echo "----------------------------------------"
echo "----- Compiling RTL-SDR Band Viewer -----"
echo "----------------------------------------"
cd /home/pi/rpidatv/src/rtlsdrview
make
cp rtlsdrview ../../bin/
cd /home/pi

# Compile Pluto Band Viewer
echo
echo "---------------------------------------"
echo "----- Compiling Pluto Band Viewer -----"
echo "---------------------------------------"
cd /home/pi/rpidatv/src/plutoview
make
cp plutoview ../../bin/
cd /home/pi

# Install SDRPlay API and compile MeteorViewer
if [ ! -f  /home/pi/rpidatv/bin/beacon ]; then
  echo
  echo "-------------------------------------------------"
  echo "----- Setting SDRPlay for install on reboot -----"
  echo "-------------------------------------------------"

  touch /home/pi/rpidatv/.post-install_actions
else
  echo
  echo "----------------------------------"
  echo "----- Compiling MeteorViewer -----"
  echo "----------------------------------"
  cd /home/pi/rpidatv/src/meteorview
  # Compile meteorview
  make
  cp meteorview ../../bin/
  cd /home/pi

  # Compile the meteor beacon and server files
  cd /home/pi/rpidatv/src/meteorbeacon
  make
  cp beacon ../../bin
  cp server ../../bin
  cd /home/pi
fi

# Compile Power Meter
echo
echo "---------------------------------"
echo "----- Compiling Power Meter -----"
echo "---------------------------------"
cd /home/pi/rpidatv/src/power_meter
make
cp power_meter ../../bin/
cd /home/pi

# Compile NF Meter
echo
echo "----------------------------------------"
echo "----- Compiling Noise Figure Meter -----"
echo "----------------------------------------"
cd /home/pi/rpidatv/src/nf_meter
make
cp nf_meter ../../bin/
cd /home/pi

# Compile Sweeper
echo
echo "---------------------------------------"
echo "----- Compiling Frequency Sweeper -----"
echo "---------------------------------------"
cd /home/pi/rpidatv/src/sweeper
touch sweeper.c                  # Force recompilation for LimeSuite update
make
cp sweeper ../../bin/
cd /home/pi

# Compile DMM Display
echo
echo "---------------------------------------"
echo "-------- Compiling DMM Display --------"
echo "---------------------------------------"
cd /home/pi/rpidatv/src/dmm
make
cp dmm ../../bin/
cd /home/pi

# Compile Noise Meter
echo
echo "---------------------------------"
echo "----- Compiling Noise Meter -----"
echo "---------------------------------"
cd /home/pi/rpidatv/src/noise_meter
make
cp noise_meter ../../bin/
cd /home/pi

# Compile and install the executable for GPIO-switched transmission (201710080)
echo "Installing keyedtx"
cd /home/pi/rpidatv/src/keyedtx
make
mv keyedtx /home/pi/rpidatv/bin/
cd /home/pi

# Compile and install the executable for GPIO-switched transmission with touch (202003020)
cd /home/pi/rpidatv/src/keyedtxtouch
make
mv keyedtxtouch /home/pi/rpidatv/bin/
cd /home/pi

# Compile the Attenuator Driver (201801060)
echo "Installing atten"
cd /home/pi/rpidatv/src/atten
make
cp /home/pi/rpidatv/src/atten/set_attenuator /home/pi/rpidatv/bin/set_attenuator
cd /home/pi

# Compile the wav2lime utility (202301140)
cd /home/pi/rpidatv/src/wav2lime
touch wav642lime.c
gcc -o wav2lime wav642lime.c -lLimeSuite
cp /home/pi/rpidatv/src/wav2lime/wav2lime /home/pi/rpidatv/bin/wav2lime
cd /home/pi

DisplayUpdateMsg "Step 8 of 10\nRestoring Config\n\nXXXXXXXX--"
/home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &

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

# Restore the user's original SDRplay Band Viewer config
cp -f -r "$PATHUBACKUP"/sdrplayview_config.txt /home/pi/rpidatv/src/sdrplayview/sdrplayview_config.txt

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

# Restore the user's original test cards if required
if test -f "$PATHUBACKUP"/images/tccw.jpg ; then     # Test card functionality included pre-update
  rm -rf "$PATHSCRIPT"/images
  cp -f -r "$PATHUBACKUP"/images "$PATHSCRIPT"
fi

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
  echo "webcontrol=disabled" >> "$PATHSCRIPT"/portsdown_config.txt
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

# Write new factory Contest Numbers File if required  202107010
if ! grep -q site1d0numbers= "$PATHSCRIPT"/portsdown_C_codes.txt; then
  # File needs updating
  cp "$PATHSCRIPT"/configs/portsdown_C_codes.txt.factory "$PATHSCRIPT"/portsdown_C_codes.txt
fi

# Streaming audio source: https://github.com/JoJoBond/3LAS

if [ ! -d "/home/pi/rpidatv/server/node_modules" ];then
  cd /home/pi/rpidatv/server/
  npm install ws wrtc
  chmod ug+x stream.sh
  cd /home/pi
fi

cp /home/pi/rpidatv/scripts/configs/asoundrc /home/pi/.asoundrc

if ! grep -q snd-aloop /etc/modules; then
  sudo sed -i '$ s/$/\nsnd-aloop/' /etc/modules
fi

# Configure the nginx web server
sudo systemctl stop nginx
rm -rf /home/pi/webroot
cp -r /home/pi/rpidatv/scripts/configs/webroot /home/pi/webroot
sudo cp /home/pi/rpidatv/scripts/configs/nginx.conf /etc/nginx/nginx.conf

DisplayUpdateMsg "Step 9 of 10\nFinishing Off\n\nXXXXXXXXX-"
/home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &

# Update the version number

cp /home/pi/prev_installed_version.txt /home/pi/rpidatv/scripts/prev_installed_version.txt
rm /home/pi/prev_installed_version.txt
rm /home/pi/rpidatv/scripts/installed_version.txt
cp /home/pi/rpidatv/scripts/latest_version.txt /home/pi/rpidatv/scripts/installed_version.txt

# Save (overwrite) the git source used
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

sleep 1
# Turn off swap to prevent reboot hang
sudo swapoff -a
sudo shutdown -r now  # Seems to be more reliable than reboot

exit
