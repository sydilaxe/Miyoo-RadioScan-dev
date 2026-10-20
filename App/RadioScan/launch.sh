#!/bin/sh
# RadioScan launcher for OnionOS. Same setup as HI5 / Jaffedrone (which mirror Onion's own GMU
# launcher): take the audio device from audioserver and use Onion's MI_AO/MI_GFX-backed SDL 1.2.
cd "$(dirname "$0")"

echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
[ -f /mnt/SDCARD/.tmp_update/script/stop_audioserver.sh ] && . /mnt/SDCARD/.tmp_update/script/stop_audioserver.sh

touch /tmp/stay_awake
unset LD_PRELOAD
export LD_LIBRARY_PATH="/mnt/SDCARD/.tmp_update/lib/parasyte:/lib:/config/lib:/mnt/SDCARD/miyoo/lib:/mnt/SDCARD/.tmp_update/lib:$LD_LIBRARY_PATH"
export RADIOSCAN_DIR="$(pwd)"

# Optional overrides (remove the # to use one):
# export RS_BUFFER=2048     # if audio crackles or drops out
# export RS_RATE=32000      # lighter on the CPU (48000 is the hardware's native rate)
# export RS_ROTATE180=0     # if the picture is upside down (the default turns it for the Mini Plus panel)
./radioscan > log.txt 2>&1

rm -f /tmp/stay_awake
