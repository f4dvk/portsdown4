#!/bin/bash

PCONFIGHOTSPOT="/home/pi/rpidatv/scripts/hotspot_config.txt"

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

###########################################################

sudo systemctl disable hostapd
sudo systemctl disable dnsmasq
sudo systemctl stop hostapd
sudo service hostapd stop
sudo service dnsmasq stop
sudo service networking restart
set_config_var hotspot "off" $PCONFIGHOTSPOT

# Si présent, suppression démarrage auto hotspot
if grep -q "iptables-restore < \/etc\/iptables.ipv4.nat" /etc/rc.local; then
  sudo sed -i "/iptables-restore < \/etc\/iptables.ipv4.nat/d" /etc/rc.local
  sudo sed -i "/^$/d" /etc/rc.local
fi

if grep -q "sudo ifconfig" /home/pi/.bashrc; then
 sed -i "/sudo ifconfig wlan0 172.24.1.1/d" /home/pi/.bashrc
 sed -i "/^$/d" /home/pi/.bashrc
fi

# Si présent, suppression inhibition dhcp wlan0
if grep -q "denyinterfaces wlan0" /etc/dhcpcd.conf; then
  sudo sed -i "/denyinterfaces wlan0/d" /etc/dhcpcd.conf
fi

# Remplacer interfaces
sudo cp /home/pi/rpidatv/scripts/configs/wifi_interfaces.txt /etc/network/interfaces

sudo ip link set wlan0 down
sudo ip link set wlan0 up

## Make sure that it is not soft-blocked
sleep 1
sudo rfkill unblock 0

sudo systemctl daemon-reload

sleep 1

exit
