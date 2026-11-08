#!/bin/sh
# Son du Pi pour la page web /touchscreen/ (touche "Pi audio") : ce que les
# applications jouent sur la carte son virtuelle (plughw:Loopback,0,2 :
# recepteur RTL-FM, decodeur multiple...) est lu ici et servi par md_audio
# (port 2007 local, relaye par nginx sur /touchscreen/audio).
# Lance par startup.sh ; relance si arecord s'arrete. Stereo, lue directement
# (plug_Loopback_1_2 de .asoundrc additionne les deux voies : niveau double).

while true; do
  arecord -q -D plughw:Loopback,1,2 -f S16_LE -r 32000 -c 2 -t raw 2>/dev/null \
    | /home/pi/rpidatv/bin/md_audio -r 32000 -c 2 -w -s 0 -l 0
  sleep 2
done
