Test build of RadioScan, an internet-radio synthesizer for the Miyoo Mini Plus (OnionOS).

**Install**
1. Download `RadioScan.zip` below and unzip it.
2. Copy the `RadioScan` folder to `/mnt/SDCARD/App/`.
3. Turn Wi-Fi on in OnionOS and open **Apps → RadioScan**.

**Not yet tested on real hardware.** If something is off, check `log.txt` in the app folder:
- No sound: edit `launch.sh` and set `RS_AUDIOSERVER=0`.
- Upside-down screen: set `RS_ROTATE180=1`.
- Crackles: set `RS_BUFFER=2048`.

MP3 streams only (AAC, Ogg and HLS stations are skipped). Some preset station URLs may have changed; replace them from the RADIO page.

See the README for controls and features.
