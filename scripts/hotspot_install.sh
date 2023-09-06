#!/bin/bash

# Variables
PCONFIGFILE="/home/pi/rpidatv/scripts/hotspot_config.txt"

CMDFILE="/home/pi/tmp/wifi_hotspot_config.txt"

# Fonction lecture des paramètres
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

set_config_var() {
lua - "$1" "$2" "$3"<<EOF > "$3.bak2"
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
mv "$3.bak2" "$3"
}

# Lecture des paramètres
SSID=$(get_config_var ssid $PCONFIGFILE)
PW=$(get_config_var wpa_passphrase $PCONFIGFILE)
MODE=$(get_config_var hw_mode $PCONFIGFILE)
CHANNEL=$(get_config_var channel $PCONFIGFILE)

sudo service hostapd status >/dev/null 2>/dev/null

if [ $? == 0 ]; then
  sudo systemctl disable hostapd
  sudo systemctl disable dnsmasq
  sudo systemctl stop hostapd
  sudo service hostapd stop
  sudo service dnsmasq stop
fi

# Désactive le dhcp wlan0
if ! grep -q denyinterfaces /etc/dhcpcd.conf; then
  sudo sed -i "/timeout 5/i\denyinterfaces wlan0\n" /etc/dhcpcd.conf
fi

# Remplacer le fichier denyinterfaces
sudo cp /home/pi/rpidatv/scripts/configs/hotspot_interfaces.txt /etc/network/interfaces

# Redemarrer dhcp et wifi
#sudo service dhcpcd restart
sudo ifdown wlan0
sudo ifup wlan0

# Configuration hostapd.conf
/bin/cat <<EOM >$CMDFILE
# This is the name of the WiFi interface we configured above
interface=wlan0

# Use the nl80211 driver with the brcmfmac driver
driver=nl80211

# This is the name of the network
ssid=$SSID

# Use the 2.4GHz band
hw_mode=$MODE

# Use channel 6
channel=$CHANNEL

# Enable 802.11n
ieee80211n=1

# Enable WMM
wmm_enabled=1

# Enable 20MHz channels with 20ns guard interval
ht_capab=[HT20][SHORT-GI-20]

# Accept all MAC addresses
macaddr_acl=0

# Use WPA authentication
auth_algs=1

# Require clients to know the network name
ignore_broadcast_ssid=0

# Use WPA2
wpa=2

# Use a pre-shared key
wpa_key_mgmt=WPA-PSK

# The network passphrase
wpa_passphrase=$PW

# Use AES, instead of TKIP
rsn_pairwise=CCMP

EOM

sudo cp $CMDFILE /etc/hostapd/hostapd.conf

# Remplacer #DAEMON_CONF dans hostapd
sudo sed -i 's\#DAEMON_CONF=""\DAEMON_CONF="/etc/hostapd/hostapd.conf"\' /etc/default/hostapd

# Configuration dnsmasq
sudo mv /etc/dnsmasq.conf /etc/dnsmasq.conf.orig
sudo cp /home/pi/rpidatv/scripts/configs/dnsmasq_config.txt /etc/dnsmasq.conf

# Configuration IPV4
sudo sed -i 's\#net.ipv4.ip_forward=1\net.ipv4.ip_forward=1\' /etc/sysctl.conf
sudo sh -c "echo 1 > /proc/sys/net/ipv4/ip_forward"

# Configuration NAT
sudo iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
sudo iptables -A FORWARD -i eth0 -o wlan0 -m state --state RELATED,ESTABLISHED -j ACCEPT
sudo iptables -A FORWARD -i wlan0 -o eth0 -j ACCEPT

sudo sh -c "iptables-save > /etc/iptables.ipv4.nat"

# Démarrage auto
if ! grep -q iptables-restore /etc/rc.local; then
 sudo sed -i "s/\"exit 0\"/\"exit\"/g" /etc/rc.local
 sudo sed -i "/exit 0/i\iptables-restore < /etc/iptables.ipv4.nat\n" /etc/rc.local
 sudo sed -i "/^$/d" /etc/rc.local
fi

if ! grep -q "sudo ifconfig" /home/pi/.bashrc; then
 sed -i "s/\"source \"/\"source \"/g" /home/pi/.bashrc
 sed -i "/source /i\sudo ifconfig wlan0 172.24.1.1\n" /home/pi/.bashrc
 sed -i "/^$/d" /home/pi/.bashrc
fi

# Démarrage des service
sudo systemctl unmask hostapd
sudo systemctl enable hostapd
sudo systemctl enable dnsmasq
sudo systemctl start hostapd
#sudo service hostapd start
sudo service dnsmasq start

set_config_var hotspot "on" $PCONFIGFILE

sleep 2

exit
