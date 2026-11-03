#! /bin/bash
# Paquets necessaires : apt seulement s'il en manque (update.sh et
# install_portsdown.sh les installent deja)
if ! dpkg -s gcc build-essential sox perl sendemail >/dev/null 2>&1; then
  sudo apt-get -y install gcc build-essential sox perl sendemail
fi
gcc ./dec406_V6.c -lm -o ./dec406_V6
gcc ./reset_usb.c -lm -o ./reset_usb
sudo chmod a+x scan406.pl
sudo chmod a+x sc*
sudo chmod a+x dec*
sudo chmod a+x reset_usb
sudo chmod a+x config_mail.pl
#./config_mail.pl
#echo -e "\nVérifier que la clé SDR est branchée\n"
#echo -e "\nVoulez-vous lancer l'application maintenant? (o/n)"
#read reponse

#if [[ "$reponse" ==  *o* ]]
#then
#	lxterminal -e "/home/pi/rpidatv/406/scan406.pl 406M 406.1M"
#fi
