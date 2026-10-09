# GPU Rendering

The default device build uses FCEUmm with GLES rendering through DRM/KMS, GBM and
EGL directly on Raspberry Pi OS Lite. It does not require X11, Wayland or a
desktop session. The software build remains available as a separate package for
comparison and fallback.

## Build and Install

`lpm pack` selects `cm0-cross-gles` by default. The equivalent commands from
the project root are:

```sh
cmake --preset cm0-cross-gles
cmake --build --preset cm0-cross-gles --parallel
cpack --config build/cm0-cross-gles/CPackConfig.cmake -B dist
```

The output is `dist/lilygo-ui-nes_0.1.0_arm64-gles.deb` (version dependent).
On the device, close the running NES application and install the local package:

```sh
sudo apt install --reinstall ./lilygo-ui-nes_0.1.0_arm64-gles.deb
```

The package declares `libegl1`, `libgles2`, `libgbm1`, `libegl-mesa0` and
`libgl1-mesa-dri` as additional runtime dependencies. Apt installs the GPU libraries
from the device's configured repositories; the cross-compilation libraries are
not copied into the application package. As with the existing CM0 BSP, the binary
targets the Debian 13 / Raspberry Pi OS trixie ARM64 ABI.

`cmake/graphics-deps.cmake` downloads eight pinned Debian ARM64 packages with
SHA-256 verification into `.cache/cm0-graphics/trixie-arm64-v1`. Headers and link
libraries are kept in an overlay separate from the original BSP sysroot. The
first GPU configuration requires network access; subsequent builds reuse the
verified cache. `LILYGO_GRAPHICS_CACHE_DIR` and `LILYGO_GRAPHICS_MIRROR` can override
the cache location and mirror without changing the pinned hashes.

For software rendering, explicitly configure and build `cm0-cross`, then package
with `cpack --config build/cm0-cross/CPackConfig.cmake -B dist`. It also uses
FCEUmm and its package is `lilygo-ui-nes_0.1.0_arm64.deb`.
Both packages have the same package identity and preserve the ROM directory and
saves. The `cm0-cross-gles` preset uses FCEUmm with GLES rendering; see
[FCEUmm Integration](05-fceumm.md).
The GLES package requires the DRM backend and rejects `--display fbdev`.

## Rendering Path

The emulator still produces native `256 x 240` RGB565 frames. On the LVGL thread,
`NesGpuFrame` uploads 120 KiB per new frame into one persistent texture using
`glTexSubImage2D`. It does not allocate a texture or expand the frame on every
update. `lv_3dtexture` draws the texture at the current layout size using GPU
nearest-neighbor sampling. The channel mapping accounts for LVGL's internal BGR
display texture, and uploads preserve the GL unpack state.

Landscape uses AppKit's `LV_DISPLAY_ROTATION_90`, matching Launcher. The
`lvgl-9.5.0-gles-display-rotation.patch` removes an extra half-turn from LVGL's
GLES quarter-turn presentation, so software DRM and GLES produce the same
physical orientation. NES multitouch applies the same inverse coordinate
transform as LVGL's pointer input.

LVGL's OpenGL draw unit caches ordinary UI drawing as textures. Some UI elements
still use software drawing when their cached textures are created. The frequently
changing NES image bypasses that path entirely. Display rotation is handled by
the GL presentation path rather than the software DRM rotation buffer.

`cmake/lv_conf_gles.h` extends the AppKit configuration only for GPU targets.
`cmake/patches/lvgl-9.5.0-gles-compat.patch` fixes GLES 2 index/vertex compatibility
and GL resource cleanup in the pinned LVGL source. AppKit releases cached GPU
objects before deleting its display and EGL context.
An additional `lvgl-9.5.0-drm-timing.patch` records DRM page-flip timestamps and
mode information when `NES_PERF=1`; see [Performance Diagnostics](04-performance.md).

## Device Checks

The CM0 Launcher runs as root and owns `/dev/dri/card0` while showing its home
screen. Launching NES from that screen performs the DRM handoff automatically.
For an SSH or standalone terminal test, stop the launcher first and use the same
device privileges:

```sh
sudo systemctl stop lilygo-ui-launcher.service
sudo lilygo-ui-nes --display drm --drm /dev/dri/card0 --orientation portrait
```

After exiting NES, restore the launcher:

```sh
sudo systemctl start lilygo-ui-launcher.service
```

`Failed to become DRM master` means display ownership could not be acquired;
no game has started at this point. Running with sudo alone does not displace an
existing DRM master. If the failure persists after stopping the launcher, inspect
other holders with `sudo fuser -v /dev/dri/card0` before stopping another process.
The ROM library uses the launching process's `$HOME/nes/rom` only. When sudo sets
`HOME=/root`, place ROMs in `/root/nes/rom` or use **Add ROM** in the library to import them.
Manual save states and battery saves use `$HOME/nes/states` (`/root/nes/states`
with that environment), shared by the software and GLES packages.

Startup reports `[NES GPU] vendor=... renderer=... version=...` followed by
`256x240 RGB565 texture; nearest-neighbor GPU scaling enabled`. A renderer such as
`llvmpipe` or `softpipe` means Mesa is rendering on the CPU. Check the board's KMS
configuration and installed GPU driver in that case. The application does not
rewrite boot configuration or select a hardware-specific overlay automatically.

The default node is `/dev/dri/card0`. Use `--drm /dev/dri/cardN` and, if needed,
`--drm-connector ID` for the display's KMS device. The user running the application
needs access to the DRM device and an available DRM master; stop other applications
that own the display before a standalone test.

GPU startup/upload errors are written to stderr. Texture initialization failures
prevent a ROM from starting; an upload failure stops the game and shows an error.
GPU support does not accelerate CPU/APU emulation. Audio runs through a separate
writer, but output backpressure can still cause dropped audio samples. Device
frame pacing and actual performance still need board testing.

The LVGL CPU and memory monitors remain enabled. Their FPS value describes LVGL
refreshes, not the physical panel rate. With `NES_PERF=1`, FCEUmm's `emu` duration
covers the complete `retro_run`, including its audio/video callbacks, so do not
add the nested `audio_gen` and `video` durations to it. The `gpu` field measures
CPU texture-submission time, and completed DRM page-flip counters measure display
presentation. See [Performance Diagnostics](04-performance.md) for the distinct
measurement scopes. Replacing the emulator core does not change the panel's
configured refresh rate.

## Offscreen Tests

The regular `host-simulator` tests continue to exercise the software path. A
separate build validates the real GLES 2 drawing path through an EGL pbuffer:

```sh
cmake --preset host-gles-test
cmake --build --preset host-gles-test --parallel
LILYGO_NES_TEST_EGL_LIBRARY=/absolute/path/to/libEGL.dylib \
LILYGO_NES_TEST_GLES_LIBRARY=/absolute/path/to/libGLESv2.dylib \
LILYGO_NES_TEST_OUTPUT_DIR="$PWD/build/host-gles-test/screenshots" \
ctest --preset host-gles-test --output-on-failure
```

On macOS, provide a matching pair of ANGLE libraries with the Metal backend.
On Linux, provide EGL/GLES libraries with a usable offscreen display. The test
does not install or redistribute these host libraries. It checks color channels,
image orientation, noninteger nearest-neighbor scaling, successive frame updates,
physical scanout at 0/90/180/270 degrees,
GL unpack-state restoration and repeated texture/display creation and deletion.
PPM images are saved for visual inspection. This does not exercise a real DRM
connector, device touch input or the board's Mesa driver.
The suite runs the gameplay path through the FCEUmm integration.
