#!/bin/sh
# RadioScan launcher for OnionOS / Miyoo Mini Plus
mydir=$(cd "$(dirname "$0")" && pwd)
cd "$mydir"

export RADIOSCAN_DIR="$mydir"
export LD_LIBRARY_PATH="$mydir/lib:/mnt/SDCARD/miyoo/lib:/mnt/SDCARD/.tmp_update/lib:/customer/lib:/config/lib:$LD_LIBRARY_PATH"

# Internal engine rate. 32000 is a good balance; 22050 saves CPU, 44100/48000 sound brighter.
export RS_RATE=32000
# Audio buffer in frames. Raise to 2048 if you hear crackles.
export RS_BUFFER=1024
# Set to 1 if the picture appears upside down on your firmware.
export RS_ROTATE180=0

# OnionOS runs an audio server that owns the sound device; route through it when it is running.
# If you get no sound, try setting RS_AUDIOSERVER=0 (stop it while RadioScan runs) below.
RS_AUDIOSERVER=1
stopped_audioserver=0
if pgrep audioserver >/dev/null 2>&1; then
    if [ "$RS_AUDIOSERVER" = "1" ] && [ -f /mnt/SDCARD/miyoo/lib/libpadsp.so ]; then
        export LD_PRELOAD=/mnt/SDCARD/miyoo/lib/libpadsp.so
    elif [ "$RS_AUDIOSERVER" = "0" ]; then
        killall audioserver 2>/dev/null; killall audioserver.mod 2>/dev/null
        stopped_audioserver=1
    fi
fi

chmod +x ./radioscan
./radioscan > "$mydir/log.txt" 2>&1
unset LD_PRELOAD

if [ "$stopped_audioserver" = "1" ]; then
    if [ -x /mnt/SDCARD/.tmp_update/bin/audioserver ]; then /mnt/SDCARD/.tmp_update/bin/audioserver &
    elif [ -x /customer/app/audioserver ]; then /customer/app/audioserver &
    fi
fi
sync
