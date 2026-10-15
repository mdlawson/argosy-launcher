# PT SkyWalker541 shader test build

This debug APK adds `OrigTexture`, `OrigTextureSize`, and `OrigInputSize` to
Argosy's built-in emulator and shader previews. It preserves the original core
frame across shader passes and keeps the existing `previousPass` sampler working.

Install `argosy-pt-skywalker-debug.apk` on an ARM64 Android handheld. The debug
app installs separately from the release app. This build uses the same cached
debug signing key as the earlier SNES audio test builds; an existing debug app
can be updated if that key is still available. Do not uninstall an existing app
without backing up its settings and saves.

Import the Standard RetroArch version of
[PT_SkyWalker541.glsl](https://github.com/SkyWalker541/PT-SkyWalker541/blob/main/Standard%20RetroArch/shaders_glsl/handheld/shaders/PT_SkyWalker541.glsl).

1. Launch a GB/GBC/GBA game with Argosy's built-in emulator, initially with no
   shaders. Confirm the baseline picture works.
2. Enable PT SkyWalker541 alone. Check white/bright areas, dark pixels, backing
   material, and drop shadows during gameplay for at least a minute. Check the
   shader preview too.
3. Try White, Bright, and All transparent-pixel modes; toggle drop shadows and
   Grid/LCD Dot. With Glare and sheen off, there should be no shader-generated
   light drift. With it on, slow light movement is intentional.
4. Put a color-correction shader before PT SkyWalker541. Its white detection and
   drop shadows should still use the original frame. Also try a preceding scaled
   shader pass, then reorder and remove passes.
5. Disable the shader, re-enable it, and launch a different game. Confirm the
   picture remains valid after texture and shader recreation.
6. If using dual displays, repeat gameplay on the display that hosts the built-in
   emulator. For a hardware-rendered core, check a game whose active image is
   smaller than the allocated framebuffer.

The Actions run includes unit tests, Android lint, and GLES instrumentation
regressions for original-frame sampling in single-pass and scaled two-pass
previews. Handheld testing remains necessary to confirm the reported flicker is
gone on the affected GPU/core. Report device, core, shader stack order, shader
version and parameter values if it persists.
