# RadioScan

A rhythm instrument for the **Miyoo Mini Plus** (OnionOS) played with **internet radio**.
Two stations become two voices. Each voice is opened and closed by its own Euclidean rhythm, the two can
cross-modulate each other (vocode, FM, ring mod, spectral morph), and the result runs through a filter,
a wavefolder, echo and space.

![play screen](docs/play.png)

## How it works

- **Two radio voices.** Each voice plays one station with one of four engines:
  - **LIVE:** the broadcast as it is (the default for voice A), with an optional sample-rate crush.
  - **LOOP:** a slice of the stream, replayed at the voice's pitch; every rhythm pulse restarts it (the default
    for voice B).
  - **WAVE:** a few milliseconds of the stream as a pitched waveform that keeps re-scanning the broadcast.
  - **GRAIN:** a cloud of grains from the last moments of the stream.

  **Focus** can make a voice listen only to speech or only to music. An optional **oscillator** (sine,
  triangle, saw or square) can be mixed into each voice for a cleaner tone or a carrier for cross-modulation.
  It's off by default; the radio comes first.
- **Two rhythms.** Each voice has a Euclidean rhythm (steps, pulses, rotate, rate, length, chance) that decides
  when it sounds. Every pulse opens the voice for its length, with soft edges. With the rhythm off, the voice
  plays continuously. Tap tempo (SELECT) and Wi-Fi sync drive both rhythms.
- **Cross-modulation.**
  - **Vocode:** A speaks through B, or B through A.
  - **FM:** one voice bends the other's pitch.
  - **Ring mod.**
  - **Morph:** blends the two voices' spectra from A toward B.
- **Sound.**
  - **Filter:** one knob from dark (low-pass) through open to thin (high-pass), plus resonance.
  - **Fold**, then **echo** (a dotted-eighth ping-pong) and **space** (reverb).
  - **Volume**, and a leveller that keeps the overall loudness steady so the radio comes through clearly.
- **Stations.** 35 are preloaded and you can add more three ways:
  1. Edit `stations.txt` on the SD card (`Name|URL|voice|music|sound`).
  2. Type a URL on the on-screen keyboard (RADIO page, **Y**).
  3. **Scan** the free [radio-browser.info](https://www.radio-browser.info) directory by genre, by name or at
     random, then audition results on either voice and save them (RADIO page, **R2**).

  HTTPS works through a built-in mbedTLS, along with ICY song titles, redirects and `.pls` / `.m3u` playlists.

## The screen

The PLAY page is for performing: the two rhythms and the sound, live.

- **Voice circles (left A, right B).** A faint inner form shows the raw broadcast; a bright outer form shows
  the voice as it sounds. The rhythm runs as dots around the rim: coloured dots are pulses, and the white one
  is the current step. Five dots underneath show how speech-like the station is.
- **Centre.** The final sound. The dot travelling over the top shows the morph position, from A on the left to
  B on the right.
- **Five circles along the bottom:** *cross → filter → fold → echo → space*. Each grows the more it changes
  the sound.
- **Abstract forms.** Every form is driven by the sound, not drawn as a waveform:
  - Loudness swells the core.
  - Brightness adds petals (3, 5, 8, 13, 21) and spin.
  - Spiky sound sharpens the petals.
  - Attacks send out ripples.
  - In the fold circle the petals bend through the folder itself.

The EDIT pages (**RADIO, VOICES, RHYTHM, ENGINE**) hold every setting. A strip at the bottom shows the voices
and the five stages, with the stage you're editing ringed.

| | |
|---|---|
| ![engine](docs/engine.png) | ![rhythm](docs/rhythm.png) |
| ![radio](docs/radio.png) | ![sound gestures](docs/playmod.png) |

## Install

1. Download `RadioScan.zip` from the latest [Release](../../releases) (or the `RadioScan-miyoo` artifact of an
   [Actions build](../../actions)) and unzip it.
2. Copy the `RadioScan` folder to `/mnt/SDCARD/App/` (so you have `App/RadioScan/launch.sh`).
3. Make sure Wi-Fi is on in OnionOS, then open **Apps → RadioScan**.

With the card mounted on a computer, `make install SD=/path/to/card` copies the built app into `App/`.

## Controls

**PLAY** (performance)

| Button | Action |
|---|---|
| D-pad up / down | rhythm A: more / fewer pulses |
| D-pad left / right | rhythm A: rotate |
| X / B | rhythm B: more / fewer pulses |
| Y / A | rhythm B: rotate |
| hold L2 + D-pad / ABXY | steps (up/down, X/B) and rate (left/right, Y/A) of that rhythm |
| tap L2 | grab fresh slices from both streams |
| hold R2 + D-pad | up/down: filter brighter / darker; left/right: morph toward A / B |
| hold R2 + X / B | fold more / less |
| hold R2 + Y / A | space less / more |
| R2 + L1 / R1 / SELECT | dice: new rhythms / new sound / hop both stations |
| L1 / R1 | pitch down / up an octave (LOOP, WAVE and GRAIN voices) |
| SELECT | tap tempo |
| START | go to EDIT |
| MENU twice | quit |

**EDIT** (RADIO, VOICES, RHYTHM, ENGINE)

| Button | Action |
|---|---|
| L1 / R1 | previous / next page |
| Up / Down | choose a row; Left / Right change it (hold B for fine steps) |
| X / L2 | grab |
| Y | roll the dice on the selected section |
| SELECT | tap tempo |
| START | back to PLAY |

**RADIO page:** A sends the selected station to voice A and B sends it to voice B. Y adds a URL, X deletes
(press twice), L2 sets the station's kind (voice / music / sound), and R2 scans the directory.

## Wi-Fi sync

RadioScan shares its clock with other units on the same Wi-Fi network that speak the same sync protocol.
Set it under **RHYTHM → CLOCK → Wi-Fi Sync**:

| Setting | Behavior |
|---|---|
| AUTO (default) | Syncs whenever another unit is around; the lowest-numbered unit leads |
| LEAD | Always leads when present |
| FOLLOW | Never leads; waits for a leader |
| OFF | Runs alone and sends nothing |

The leader sets the tempo; tap tempo on the leader and every follower jumps to the new downbeat. Followers
nudge their clock speed by up to 3% to stay in phase, typically within a few milliseconds. Sync moves RadioScan's
two rhythms and the echo time; the radio streams themselves just keep playing. If the leader stops its
transport, followers hold their rhythms too.

The badge next to the tempo shows the state: an empty ring while searching, then *lead* or *follow* once
linked. Each unit sends 32 bytes ten times a second, so sync adds almost nothing on top of the two radio
streams. All units must be on the same network, and the network must allow devices to see each other. Guest
networks and some public Wi-Fi block this; a phone hotspot works well and also gives the radio its internet.

## Building

The Miyoo build uses the OnionOS toolchain image:

```sh
docker run --rm -v "$PWD":/root/workspace aemiii91/miyoomini-toolchain:latest bash -lc \
  'cd /root/workspace && make package \
     CROSS=/opt/miyoomini-toolchain/usr/bin/arm-linux-gnueabihf- \
     SYSROOT=/opt/miyoomini-toolchain/usr/arm-linux-gnueabihf/sysroot'
# -> dist/RadioScan.zip
```

If Docker Hub rate-limits you, `mirror.gcr.io/aemiii91/miyoomini-toolchain:latest` is the same image.

Desktop build for development (needs SDL 1.2 headers, e.g. `libsdl1.2-dev` or `libsdl1.2-compat-dev`):

```sh
make
RADIOSCAN_DIR=App/RadioScan ./build/desktop/radioscan
```

Keyboard on desktop, matching the Miyoo's key codes:

| Keys | Buttons |
|---|---|
| arrows | D-pad |
| Space / L-Ctrl / L-Shift / L-Alt | A / B / X / Y |
| E / T | L1 / R1 |
| Tab / Backspace | L2 / R2 |
| R-Ctrl / Enter / Esc | SELECT / START / MENU |

### Releasing

Any of these builds, tests and publishes a GitHub Release with `RadioScan.zip` attached (notes come from
`.github/release-notes.md`):

- **Actions → build → Run workflow**, with a tag such as `v0.2.0` filled in.
- Push a version tag: `git tag v0.2.0 && git push origin v0.2.0`.
- Create a release on the GitHub website; the zip is added to it automatically.

### Testing without the internet

`tools/fake_icecast.py` generates a speech-like and a music-like MP3 stream and serves them the way Icecast
does: ICY metadata, a redirect, a `.pls` playlist, a fake directory, and optional HTTPS with a throwaway CA.
`radioscan --render <seconds> <out.wav> [param=value ...]` runs both streams through the synth offline.
It reports level, the voice/music verdict for each stream, and DSP cost.

`radioscan --synctest LEAD|FOLLOW|AUTO [bpm] [seconds]` runs only the clock and Wi-Fi sync, then prints
whether it found peers, who led and how far apart the units ended up. Run two or more on the same network.

CI runs both on every push.

## Settings

All parameters are saved to `settings.cfg` in the app folder, including when OnionOS powers off. When a new
version changes the sound defaults, older settings keep their stations but start from the new sound.

`launch.sh` uses the same setup as the sibling apps HI5 and Jaffedrone, which mirror Onion's own GMU launcher.
It stops Onion's audio server while RadioScan runs, uses Onion's MI_AO/MI_GFX-backed SDL 1.2, sets the
performance CPU governor, and keeps the device awake. It has a few optional switches:

- `RS_BUFFER`: audio buffer size (raise it to 2048 if you hear crackles).
- `RS_RATE`: engine rate (48000 by default, the hardware's native rate; 32000 is lighter on the CPU).
- `RS_ROTATE180`: the picture is turned 180° for the Mini Plus panel by default; set it to 0 if your screen
  shows it upside down.

## Performance

The engine runs at 48 kHz. On the desktop it takes about 1–3% of one 2.1 GHz Xeon core (vocode and morph are
the heaviest). That suggests roughly 10–25% of one Cortex-A7 core on the Mini Plus, plus a few percent for each MP3 decoder,
leaving plenty of headroom on the dual-core chip. Denormals are flushed to zero in the audio thread so long
reverb tails stay cheap.

## Limitations

- MP3 streams only. AAC, Ogg/Opus and HLS (`.m3u8`) stations are skipped or reported as unsupported. The
  directory scan only lists MP3 stations.
- Station URLs change over time. If a preset stops working, remove it (X twice on the RADIO page) or scan for
  a replacement.
- The voice detector uses simple speech features (pauses between syllables, fricative bursts). It works well on
  clear talk against dense music, but a sparse solo instrument or a busy talk show can fool it.
- First hardware test: the screen orientation and the low volume are fixed in this version.

## Credits

- [minimp3](https://github.com/lieff/minimp3) (CC0) for MP3 decoding.
- [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) (Apache-2.0) for HTTPS.
- CA certificates from Mozilla via [certifi](https://github.com/certifi/python-certifi) (MPL-2.0).
- Font rendered from DejaVu Sans Mono (Bitstream Vera license).
- Station directory: [radio-browser.info](https://www.radio-browser.info), a free community database.
