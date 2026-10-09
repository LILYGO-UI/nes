# FCEUmm Integration

FCEUmm is the application's only emulation core, linked statically through the
libretro API without a RetroArch dependency. Every build logs `nes: core=fceumm`
when a game starts.

## Source Submodule

`third_party/fceumm` contains the complete upstream
[Libretro FCEUmm repository](https://github.com/libretro/libretro-fceumm.git) as a
Git submodule. The parent repository pins its commit; initialize it and AppKit
before configuring a build, and update them after pulling project changes:

```sh
git submodule update --init --recursive
```

The application build integration stays in `cmake/Fceumm.cmake`; no upstream
source edits are required. The pinned revision and licensing details are
recorded in [FCEUmm Origin](../third_party/FCEUMM_ORIGIN.md).

## FCEUmm Device Package

`lpm pack` uses this preset by default. The equivalent CMake steps are:

```sh
cmake --preset cm0-cross-gles
cmake --build --preset cm0-cross-gles --parallel
cpack --config build/cm0-cross-gles/CPackConfig.cmake -B dist
```

This produces `lilygo-ui-nes_0.1.0_arm64-gles.deb` (version dependent),
with GLES rendering, the on-screen LVGL monitors and FCEUmm. On the board,
close the running application before installing:

```sh
sudo apt install --reinstall ./lilygo-ui-nes_0.1.0_arm64-gles.deb
```

The launcher starts FCEUmm without environment changes. `host-simulator` builds
the desktop simulator, `host-gles-test` validates offscreen GPU rendering, and
`cm0-cross` builds the device package with software rendering.

## Check the Device

Run a game for at least two minutes after startup to check audio drift as well
as frame cadence.
For terminal/SSH testing, first stop the Launcher service so it releases DRM;
run NES as root, matching the Launcher service's device privileges:

```sh
sudo systemctl stop lilygo-ui-launcher.service
sudo env NES_PERF=1 lilygo-ui-nes --display drm --orientation portrait 2>&1 | tee fceumm.log
```

After exiting the application, restore the launcher:

```sh
sudo systemctl start lilygo-ui-launcher.service
```

FCEUmm uses a dedicated emulation thread, RGB565 frame delivery and LVGL/GLES.
LVGL remains on the main thread; device audio uses a separate asynchronous
writer. Audio runs at 48 kHz on both host and device. The regional frame rate
comes from the core after loading.

`NES_PERF=1` reports generated, copied and submitted frame rates plus average
and maximum stage durations. FCEUmm's `emu` covers the entire `retro_run`,
including video/audio callbacks; `video` and `audio_gen` are nested measurements
and must not be added again. See [Performance Diagnostics](04-performance.md) for
field definitions and measurement limits. The overlay FPS and submission rates
do not measure physical panel refreshes; use the completed DRM flip diagnostics
when checking presentation cadence.

To isolate the output device, repeat runs with `NES_AUDIO=0`. Audio synthesis
still runs, but SDL/aplay output is disabled. `NES_AUDIO_PLAYER=/absolute/path`
can override `aplay` for device diagnostics; it receives the normal aplay
command-line arguments without a shell.

## Compatibility and Saves

FCEUmm supports upstream iNES/NES 2.0 loading and mapper implementations, with
automatic region detection and core-reported timing. Its pinned source revision
and licenses are recorded in [FCEUmm Origin](../third_party/FCEUMM_ORIGIN.md).
The application scans only uncompressed `.nes` files, and its library metadata parser uses classic
iNES fields. NES 2.0 extended metadata may therefore appear incomplete in the
library even when FCEUmm can load the game. This integration provides no FDS
disk/BIOS workflow.

Classic iNES PAL detection primarily follows upstream filename tags such as
`(E)`, `(Europe)` and `(PAL)`, not the iNES byte 9 flag alone. NES 2.0 uses its
byte 12 region field. Confirm the selected rate in the `fceumm: ... Hz` startup
log; the library's PAL/NTSC metadata is not a guarantee of the detected region.
The adapter also limits ROM files to 64 MiB and checks declared PRG/CHR/trainer
sizes, including NES 2.0 exponent/multiplier encoding, before passing them to
FCEUmm. See [ROMs and Runtime](02-roms-and-runtime.md) for format details.

Check graphics, sound, combined input, mapper behavior and restored battery RAM
using the games intended for the board. Compatibility depends on each ROM and
the upstream implementation. Emulation does not increase the physical panel
refresh rate or resolve unstable panel timing.

ROMs are loaded from `$HOME/nes/rom`; all saves use `$HOME/nes/states` and the
complete ROM filename. For `game.nes`, the files are `game.nes.state` for a manual
state and `game.nes.sav` for battery RAM.

The game page's **Save** and **Load** buttons use FCEUmm's `retro_serialize()` and
`retro_unserialize()` on the emulation thread between frames. Each ROM has one
manual state, replaced by Save and restored explicitly by Load. State files
include format, ROM identity and integrity checks; writes use a temporary file
and atomic rename. The UI reports the operation result and disables both buttons
while a request is pending. Ordinary failures leave the game running.

Battery saves use raw `.sav` files. FCEUmm uses the save-memory
size reported by the loaded core, which is not always 8 KiB. It loads saves
before emulation and writes a temporary file followed by an atomic rename on a clean stop. Missing
saves are created. An existing unreadable or wrongly sized save is preserved
without replacement and a warning is logged. Back up important saves before
upgrading; matching sizes alone do not establish save compatibility with older
packages.
The ROM importer copies only ROM files, and deleting a ROM preserves both save
files. See [ROMs and Runtime](02-roms-and-runtime.md#battery-ram-and-save-states)
for migrating saves from the old directory layout.

## Validation

Host tests use generated ROMs to check core frames, audio, controller mapping,
battery RAM, invalid ROMs and save preservation. `nes-fceumm` covers the FCEUmm
adapter. `nes-emulator` checks repeated game sessions, frame pacing, failure
recovery and prompt stopping. Portrait and landscape UI tests and the offscreen
`nes-gpu` test exercise the same FCEUmm integration.
Device-audio tests use a local helper process to check PCM transmission, missing
or exiting players and a player that never reads. No sound device is required.

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator --output-on-failure
```

These tests establish integration behavior, not compatibility with every game
or performance on the board. Keep the board logs and actual-game results when
assessing a release.
