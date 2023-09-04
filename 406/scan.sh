#!/bin/bash

PATH_406CONFIG="/home/pi/rpidatv/406/config.txt"

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

FREQLOW=$(get_config_var low $PATH_406CONFIG)
FREQHIGH=$(get_config_var high $PATH_406CONFIG)
CHECKSUM=$(get_config_var no_checksum $PATH_406CONFIG)
INPUT=$(get_config_var input $PATH_406CONFIG)
CARD=$1

if [ "$CHECKSUM" = "1" ]; then
  CHECKSUM="no_checksum"
else
  CHECKSUM=""
fi

if [ "$INPUT" = "rtl" ]; then
  INPUT=""
fi

/home/pi/rpidatv/406/scan406.pl $FREQLOW"M" $FREQHIGH"M" 0 $CARD $CHECKSUM $INPUT

exit
