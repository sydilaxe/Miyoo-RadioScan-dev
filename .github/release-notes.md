RadioScan, an internet-radio synthesizer for the Miyoo Mini Plus (OnionOS).

**Install**

With SSH on (Onion: Apps → Tweaks → Network → SSH), run this on a computer on the same network:

```sh
curl -fsSL https://raw.githubusercontent.com/sydilaxe/RadioScan/main/install.sh | bash
```

Or by hand:
1. Download `RadioScan.zip` below and unzip it.
2. Copy the `RadioScan` folder to `/mnt/SDCARD/App/`.
3. Turn Wi-Fi on in OnionOS and open **Apps → RadioScan**.

From v0.8.0 on, RadioScan checks for new releases when it starts and offers to update itself.

If something is off, check `log.txt` in the app folder (it notes any late audio
buffers), and try these in `launch.sh` (uncomment the line):
- Crackles or dropouts: `export RS_BUFFER=4096` (the default is 2048; 1024 responds faster if the sound is steady).
- Upside-down screen: `export RS_ROTATE180=0` (the default turns the picture for the Mini Plus panel).
- Lighter on the CPU: `export RS_RATE=32000`.

MP3 streams only (AAC, Ogg and HLS stations are skipped). Your own `.wav` files can be voices too: put them in `App/RadioScan/wav/`. Some preset station URLs may have changed; replace them from the RADIO page.

See the README for controls and features.
