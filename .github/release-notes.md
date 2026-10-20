Test build of RadioScan, an internet-radio synthesizer for the Miyoo Mini Plus (OnionOS).

**Install**
1. Download `RadioScan.zip` below and unzip it.
2. Copy the `RadioScan` folder to `/mnt/SDCARD/App/`.
3. Turn Wi-Fi on in OnionOS and open **Apps → RadioScan**.

**Not yet tested on real hardware.** If something is off, check `log.txt` in the app folder (it notes any late audio
buffers), and try these in `launch.sh` (uncomment the line):
- Crackles or dropouts: `export RS_BUFFER=2048`.
- Upside-down screen: `export RS_ROTATE180=0` (the default turns the picture for the Mini Plus panel).
- Lighter on the CPU: `export RS_RATE=32000`.

MP3 streams only (AAC, Ogg and HLS stations are skipped). Some preset station URLs may have changed; replace them from the RADIO page.

See the README for controls and features.
