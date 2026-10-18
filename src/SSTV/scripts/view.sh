#!/bin/bash
# Affiche une image sur l'ecran connecte au Raspberry Pi, directement sur le
# framebuffer console (sans serveur X), exactement comme le fait Portsdown4
# pour ses propres captures (cf. scripts view.sh / snap.sh / a_remote.sh de
# portsdown4, qui utilisent tous "fbi -T 1 -noverbose -a").
#
# Utilisation : view.sh <image.bmp|image.jpg|image.png>

IMAGE="$1"

if [ -z "$IMAGE" ] || [ ! -f "$IMAGE" ]; then
  echo "Usage: $0 <fichier_image>"
  exit 1
fi

sudo killall -9 fbi >/dev/null 2>/dev/null
sleep 0.1

sudo fbi -T 1 -noverbose -a "$IMAGE" >/dev/null 2>/dev/null &
