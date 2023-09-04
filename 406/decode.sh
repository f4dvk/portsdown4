#! /bin/bash
sox -t alsa default -t wav - lowpass 3000 highpass 400  2>/dev/null | ./dec406_V6






