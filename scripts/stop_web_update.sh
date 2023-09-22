#!/bin/bash

PID=$(pgrep -f screen_grab_for_web.sh)

if [ $PID > 0 ]; then
  sudo kill $PID
fi

exit
