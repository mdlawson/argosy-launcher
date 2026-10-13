# Ceres audio comparison

This standalone diagnostic tool replays the SPC700/DSP snapshot from the
uploaded Ceres save state using Snes9x `fae2fea08f74180759ef540ee94259213f503480`.
It creates two 20-second WAV files. The original path uses the core's existing
mixing; the experimental path ramps voice-volume changes over 64 samples,
approximately 2 milliseconds. Sample interpolation, envelopes, game code,
register writes, and playback rate use the same implementation in both runs.
This is a comparison experiment, not an Argosy patch or a complete emulator.

Extract the ZIP, copy `ceres-snes9x.state` beside `CeresAudioCompare.exe`, and run
from PowerShell in that folder:

```powershell
.\CeresAudioCompare.exe .\ceres-snes9x.state
```

Listen to `ceres-original.wav` and `ceres-smoothed.wav` with the same player and
volume. Report whether the pulsating crackle is reduced, unchanged, or worse.
The tool also creates `ceres-volume-writes.csv`, identifying changed voice-volume
registers and their PCM frame positions. It reads the state without modifying it;
it makes no network requests and does not access the installed Android app.

Only uncompressed Snes9x snapshot version 14 is supported. This tool is intended
for this stable Ceres scene: freezing the main CPU's communication ports would
not reproduce gameplay-driven audio changes in a general scene. Gaussian sample
interpolation is selected because it reproduced the supplied raw recording
exactly. The WAVs use the core's native 32040 Hz rate.

## Findings

Replaying the supplied state reproduced all 639747 stereo frames of the raw
recording byte for byte, after a 131940-frame alignment offset. In that recording,
91 of 95 strong second-difference peaks occur within three samples of a channel 1
volume-register change. Original and smoothed runs have identical complete
register-write traces. The experimental ramp removes the large discontinuities
at those writes while retaining the evolving ambience. Audible improvement still
requires a listening comparison. This does not establish the analog output of a
physical SNES.

No uploaded state, recording, or game content is included in this download.
Source code and upstream license notices are included under `source/`.

## Build the included source

With a C++17 compiler and CMake installed, from the extracted ZIP folder:

```sh
cmake -S source/tool -B build -DSNES9X_SOURCE_DIR="%CD%/source/snes9x" -DLIBRETRO_COMMON_DIR="%CD%/source/libretro-common"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Those commands use Windows Command Prompt's `%CD%` expansion. The included
Snes9x source is already patched; do not run `patch_source.py` again on it.
The patch source shows the two hooks added to the pinned upstream core. The
comparison implementation lives in `source/tool/CeresAudioCompare.cpp`.
