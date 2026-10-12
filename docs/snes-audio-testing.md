# Argosy SNES audio comparison, report build

This debug APK is based on Argosy 2.20.5 and installs as
`com.nendo.argosy.debug`, labelled **Argosy DBG**. It saves diagnostics inside
the app and supports selecting the resampler with private marker files.
Logcat is optional. The report contains the actual selected mode, the core's
reported name/version, sample rates, FIFO shortages, dropped samples, and
Android audio underrun counts.

The original and streaming resamplers remain available. Their audible effect
on the Retroid Pocket Classic is unconfirmed. Use the same core, game scene,
audio route, settings, and volume in both passes, at normal 1x speed.

## Windows helper

Extract the downloaded ZIP and open a terminal in that folder. Make sure
`adb devices` shows the Pocket Classic as `device`. Install the included APK:

```sh
adb install -r argosy-2.20.5-audio-debug.apk
```

If Android reports `INSTALL_FAILED_UPDATE_INCOMPATIBLE`, use the backup and
migration steps below. Future builds on this branch cache the debug signing key.

For the original resampler:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Collect-ArgosyAudio.ps1 -Mode legacy -Core snes9x
```

The helper selects the mode and opens Argosy DBG. Exit any running game, launch
Super Metroid with Snes9x, reach your scene, and listen for at least 30 seconds.
Press Enter in the terminal when finished. The helper retrieves the app's
report and saves a timestamped text file in the current folder. It checks that
the report contains the chosen mode and audio statistics.

Exit the game, then repeat with the streaming resampler:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Collect-ArgosyAudio.ps1 -Mode streaming -Core snes9x
```

Repeat with `-Core bsnes` while selecting bsnes in the app. The `-Core` argument
labels the output file; the core used by the app is recorded in the report.
The execution-policy argument applies to this helper invocation only.

## If the debug APK signing key changed

These steps apply only if `adb install -r` reports
`INSTALL_FAILED_UPDATE_INCOMPATIBLE`. Exit the game through its menu first.
Back up the debug app's data before removing it:

```sh
adb shell am force-stop com.nendo.argosy.debug
cmd /c "adb exec-out run-as com.nendo.argosy.debug tar -cf - . > argosy-debug-backup.tar"
tar -tf argosy-debug-backup.tar
```

Continue only if both archive commands succeed and the listing shows the app's
files and databases. If either command reports an error, keep the debug app
installed and send the error. The backup contains the debug app's saves,
configuration, and downloaded cores; keep it until the restored app is checked.

Remove only the debug package, install the new APK, and restore the backup:

```sh
adb uninstall com.nendo.argosy.debug
adb install argosy-2.20.5-audio-debug.apk
cmd /c "adb exec-in run-as com.nendo.argosy.debug tar -xf - < argosy-debug-backup.tar"
adb shell am start -n com.nendo.argosy.debug/com.nendo.argosy.MainActivity
```

Check that the debug library, settings, and saves are restored before testing.
Do not uninstall the official Argosy app. `cmd /c` keeps the tar archive's binary
bytes intact when these commands are invoked from Windows PowerShell.

## Manual capture in Windows Command Prompt or PowerShell

Exit the game before changing modes. Select the original resampler:

```sh
adb shell run-as com.nendo.argosy.debug rm -f files/audio-streaming
adb shell run-as com.nendo.argosy.debug touch files/audio-legacy
```

Launch the game and listen to the scene for at least 30 seconds, then retrieve
the report:

```sh
adb shell run-as com.nendo.argosy.debug cat files/audio-diagnostics.txt > snes9x-legacy.txt
```

Exit the game and select the streaming resampler:

```sh
adb shell run-as com.nendo.argosy.debug rm -f files/audio-legacy
adb shell run-as com.nendo.argosy.debug touch files/audio-streaming
```

Relaunch, listen, and retrieve:

```sh
adb shell run-as com.nendo.argosy.debug cat files/audio-diagnostics.txt > snes9x-streaming.txt
```

These are text reports, so redirection works in both Windows shells. Retrieve
a report before launching another game: each game launch starts a fresh report.
Marker selection takes precedence over `debug.argosy.audio.legacy`; the old
property is used only if neither marker is present.

## Send back

- The reports and whether each run sounded improved, unchanged, or worse.
- Whether the original-resampler debug run reproduces the official app's crackle.
- The game region/revision, scene, and audio output route.

`AudioDiag config` shows `resampler=legacy` or `resampler=streaming`.
`AudioDiag stats` is written about every two seconds from the emulation thread.
Its counts are cumulative for that Audio instance. Startup shortages can occur;
the useful signal is whether counts continue increasing during the test scene.
`xruns=-1` means the Android backend does not expose an underrun count.

The report is written and flushed by the emulation thread, outside the real-time
audio data callback. Diagnostics and mode selection are debug-only. The native
streaming resampler uses fixed storage without callback allocations.

The source changes are on `debug/snes-audio-test-20261012`. Its GitHub workflow
runs the resampler regression suite and checks the Windows helper's syntax
before building and publishing the APK.
