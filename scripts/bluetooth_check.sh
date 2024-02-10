#!/bin/bash

device=$(bluetoothctl paired-devices | awk '{ print $2 }')
status=$(bluetoothctl info $device | grep Connected | awk -F ":" '{print $2}')

echo $status

#bluetoothctl --timeout 20 scan on

exit
