# AutoMaster — auto-mastering VST3 for FL Studio

A mastering effect for the **Master channel**. It listens to your track, guesses the genre, loads a
matching preset, sets the drive to reach a target loudness, and keeps the output under a true-peak
ceiling so it does not clip. Everything stays editable, and there are 10 presets to explore.

## Signal chain

```
input → drive → high-pass 22 Hz → Low shelf (100 Hz) → Presence (3.2 kHz) → Air shelf (9 kHz)
      → 3-band glue compressor (140 Hz / 3 kHz, bass kept mono, Width on the rest)
      → [2x oversampled] harmonic exciter (Crisp) → soft clipper
      → true-peak limiter (1.5 ms lookahead) → fast second limiter stage → output
```

## Build (Windows, for FL Studio)

1. Install **Visual Studio 2022** (workload "Desktop development with C++"), **CMake** and **Git**.
2. In this folder:
   ```
   cmake -B build -G "Visual Studio 17 2022"
   cmake --build build --config Release --target AutoMaster_VST3
   ```
   (JUCE 8.0.9 is downloaded automatically the first time.)
3. Copy `build/AutoMaster_artefacts/Release/VST3/AutoMaster.vst3` to
   `C:\Program Files\Common Files\VST3\`
4. In FL Studio: *Options → Manage plugins → Find installed plugins* (verify "Fast scan" off), then
   add **AutoMaster** on the **Master** insert, last in the chain.

macOS: `cmake -B build -G Xcode` then build the `AutoMaster_VST3` target; the plugin goes to
`~/Library/Audio/Plug-Ins/VST3`.

## How to use

1. Put AutoMaster on the Master channel.
2. Press **Analyse**, play the loudest 10–60 s of the song, press **Stop + apply**.
   The plugin reports the genre guess, tempo, input loudness and what it changed.
3. Play the whole song once, press **Match target**. It nudges Drive so the measured integrated
   loudness hits the target (a second press fine-tunes it). **Reset meters** restarts the measurement.
4. Fine-tune by ear. **Air** is the main brightness control, **Crisp** adds harmonics (subtle by design),
   **Glue** adds cohesion, **Clipper** shaves transients before the limiter.

## Notes

- Loud presets use a **-2 dB ceiling**: Spotify recommends true peak below -2 dB when a master is louder
  than -14 LUFS (its normalisation target). Streaming services turn loud masters down, so the loudest
  setting is not always the best-sounding result on Spotify/YouTube. Compare with *Streaming Safe (-14)*.
- Genre detection is a transparent rule-based guess (tempo, rhythm regularity, spectral tilt, bass weight,
  width, dynamics), not machine learning. Always check by ear; pick the preset yourself if it guesses wrong.
- The plugin reports its latency (about 3.7 ms at 44.1 kHz) so FL Studio compensates it.

## Tests

`cmake -B build -DAUTOMASTER_BUILD_TESTS=ON` builds `AutoMasterTest`, a headless harness that renders synthetic
tracks and pink noise; `Tests/check.py` and `Tests/tone_check.py` measure the results independently
(pyloudnorm LUFS, 4x true peak, per-octave response).
