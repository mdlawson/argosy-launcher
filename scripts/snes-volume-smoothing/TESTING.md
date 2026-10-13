# Snes9x volume-smoothing device test

This package contains an arm64 Android Snes9x test core, based on
`fae2fea08f74180759ef540ee94259213f503480`, with the same 64-sample voice-volume
ramps as the Ceres comparison that sounded clean. The core identifies itself as
`1.63 fae2fea volume-smooth64-test` in Argosy's diagnostic report.

The smoothing acts on each emulated sound channel before mixing, covering
left and right independently. Gaussian/cubic/sinc sample interpolation and
envelope behavior remain the upstream implementations. The existing save-state
byte format is retained. Ramps are seeded from restored volume registers after
loading a state and on reset; saving a rewind snapshot does not restart a ramp.
The ramp history is not serialized, so a load seeds fresh audio history.

This is an optional sound enhancement. The original core applies abrupt
volume changes, and that behavior has not been established as an emulation bug.
No stock Snes9x or bsnes option implements this volume smoothing. This test
package targets Snes9x only; bsnes needs a separate implementation and test.

## Install the smoothed test core

Use the existing Argosy DBG APK and its downloaded Snes9x core. Extract this ZIP,
open PowerShell in the extracted folder, then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Test-SnesVolumeSmoothing.ps1 -Mode smoothed
```

The helper asks you to save and exit the game, stops the debug app to unload
the old library, backs up the original core, verifies the transfer, and replaces
the debug app's Snes9x library. It opens Argosy DBG afterward. Launch Super Metroid
with Snes9x, reach the same Ceres scene, and listen at the same volume. Also check
normal gameplay sound and loading your existing save state.

## Return to the original core

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Test-SnesVolumeSmoothing.ps1 -Mode original
```

Follow the save/exit prompt again. The helper verifies the original backup and
restores it; the backup is kept for subsequent comparisons. Its private path is
`files/libretro/cores/snes9x_libretro_android.so.pre-volume-smoothing`. All changes
are confined to `com.nendo.argosy.debug`. The official app and game saves are not
replaced. Do not use Core Management's update/delete operations on Snes9x during
the comparison, because those would replace or remove the selected test library.

If an ADB operation fails, keep the backup and report the error. Before the final
rename, a transfer failure leaves the current installed core in place. The helper
requires a working downloaded Snes9x core and refuses to replace a missing core.

## Validation and source

The actual patched DSP was replayed against the supplied Ceres sound state.
Its output matches the accepted smoothed comparison byte for byte. Replaying
with frequent rewind-style snapshot saves, reloading the same state in a running
DSP, and soft-resetting before a reload produce the same samples. Neutral sound
states and ramp endpoint/sign/stereo tests also run in the build workflow.

Corresponding patched upstream source and the patch/test scripts are included
under `source/`, retaining the upstream license notices. No game, uploaded state,
or recording is included. Build the core with the Android NDK's `ndk-build`, using
absolute paths for `APP_BUILD_SCRIPT` and `NDK_APPLICATION_MK`, `APP_ABI=arm64-v8a`,
`APP_PLATFORM=android-23`, and `APP_SUPPORT_FLEXIBLE_PAGE_SIZES=true`. The core's
existing `libretro/jni/Android.mk` and `Application.mk` select the source and static
C++ runtime. `BUILD.txt` records the toolchain used by the published build.
