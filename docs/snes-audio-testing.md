# Argosy SNES audio comparison

This arm64 debug APK is based on Argosy 2.20.5, commit
`6a7efc43e`. It installs as `com.nendo.argosy.debug`, alongside the official
app, with its own library, settings, downloaded cores, and saves. Keep the
official app installed.

The APK includes the original resampler and a streaming linear resampler that
preserves interpolation position and sample history between audio callbacks.
The streaming path uses a two-input-frame delay, about 0.06 ms at 32 kHz.
Pitch-preserving fast-forward continues to use the existing SoundTouch path.
The fix has passed host tests; its effect on the Retroid Pocket Classic still
needs confirmation.

## Install and set up

Enable USB debugging, connect the device, and accept its ADB authorization prompt.
Download `argosy-2.20.5-audio-debug.apk` into your computer's current directory:

```sh
adb devices
adb install -r argosy-2.20.5-audio-debug.apk
adb shell am start -n com.nendo.argosy.debug/com.nendo.argosy.MainActivity
```

Set up the debug app, add the Super Metroid ROM, and download Snes9x and bsnes.
Use the same game revision, scene, audio route, settings, and volume for both
passes. Use normal 1x playback without fast-forward or rewind. Keep the same
downloaded core throughout a pair of runs.

## First pass: original resampler

Exit any running game through the emulator menu. In a terminal on your computer:

```sh
adb shell setprop debug.argosy.audio.legacy 1
adb logcat -T 1 -v threadtime -s libretrodroid:I AndroidRuntime:E "*:S" > snes9x-legacy.txt
```

Leave the log command running. Launch Super Metroid using Snes9x in the debug
app, reach the test scene, and listen for at least 30 seconds. Record whether
the original crackle is reproduced. Press Ctrl+C in the terminal to stop logging.

## Second pass: corrected resampler

Exit the game through the emulator menu, then:

```sh
adb shell setprop debug.argosy.audio.legacy 0
adb logcat -T 1 -v threadtime -s libretrodroid:I AndroidRuntime:E "*:S" > snes9x-streaming.txt
```

Relaunch the game, listen to the same scene for at least 30 seconds, and press
Ctrl+C to stop logging. The property is read when the game's audio stream is
created, so relaunch the game after every mode change.

Repeat these two passes with bsnes, using `bsnes-legacy.txt` and
`bsnes-streaming.txt` as the log filenames. The property affects this debug
build only and resets on device reboot.

## Send back

- The log files and whether each run sounded clean, improved, unchanged, or worse.
- Whether the original-resampler debug run reproduces the official app's crackle.
- The game region/revision and scene description.
- Whether you listened through speakers, wired headphones, USB audio, or Bluetooth.

Each run should contain `AudioDiag core` and `AudioDiag config` near launch.
Check that `resampler=legacy` appears in the first pass and `resampler=streaming`
in the second. Report any setprop error or missing mode change.

`AudioDiag stats` appears about every two seconds from the emulation thread.
Counts are cumulative for that Audio instance. `shortage_frames` counts silence
inserted when the emulator-side FIFO runs short; `dropped_frames` counts audio
that did not fit into that FIFO. `xruns` reports Android stream underruns, with
`-1` meaning the backend does not expose a count. Startup shortages are possible;
the useful signal is whether counts keep increasing during the test scene.

## Validation and source

- Debug APK assembly and signature verification.
- Six rate pairs, including 32040 to 48000 Hz, with variable callback sizes.
- Comparison against a continuous interpolation reference, stereo channel
  separation, changing rates, full-scale samples, empty input, and reset.
- AddressSanitizer and UndefinedBehaviorSanitizer checks; leak detection disabled
  because this execution environment uses ptrace.
- Repository added-line smell check and whitespace check.

The `debug/snes-audio-test-20261012` branch contains the source changes.
Diagnostics and the ADB selector are compiled only in debug builds. The new
resampler uses fixed storage without allocations in its audio callback.

The host tests can be run from the repository root:

```sh
cmake -S libretrodroid/src/main/cpp/tests/resampler -B /tmp/argosy-resampler-tests
cmake --build /tmp/argosy-resampler-tests
ctest --test-dir /tmp/argosy-resampler-tests --output-on-failure
```
