# ROMs and Runtime

[English README](../README.md) | [Chinese README](../README.zh-CN.md) | [Architecture](00-overview.md)

## ROM Library

The application runs local, user-provided ROMs. No ROM downloads or commercial
ROMs are included. Use files that you are legally entitled to run.

### ROM Search Paths

The library uses only `$HOME/nes/rom` for the user running the application. For
example, `HOME=/root` uses `/root/nes/rom`. `NES_ROM_DIR` is no longer supported,
and other device directories are not scanned. `HOME` must be a nonempty absolute
path; an unset, empty, or relative value leaves the library unconfigured and
shows an error instead of selecting another directory.

The app creates `$HOME/nes/rom` if it is missing, at startup and whenever the library
is refreshed; a creation failure displays a filesystem error. Fix the path or
permissions and press Refresh to retry. Scans are not
recursive, so place ROM files directly in that directory. The library collects
at most 128 files, then sorts them by title without case sensitivity. When the directory
contains more files than the limit, filesystem iteration order determines which
files are included. ROM contents are not deduplicated.

The library uses a full-width, single-column list in both orientations. It has
no summary panel or ROM counts. Add ROM is the last item in the list and scrolls
with it. When no ROMs are available, the list contains only Add ROM, without an
empty-library card or Ready status. Refresh, Settings, and Help remain in a fixed
toolbar below the list, with 77-pixel buttons and 16-pixel gaps.
Help shows the actual, expanded ROM and save directory
paths (for example, `/root/nes/rom` and `/root/nes/states`), with wrapping text and an independently
scrollable content area. Its Back button returns to the library.

To prepare the host library manually:

```sh
mkdir -p "$HOME/nes/rom" "$HOME/nes/states"
lpm start --no-build --foreground
```

Copy your ROM files into that directory before launching, or use the refresh
button after copying them. Existing ROMs in older locations, including `$HOME/nes`,
are not migrated automatically; move them into `$HOME/nes/rom` and refresh, or use
the import button described below to copy them into the library.

### Importing ROMs

Tap **Add ROM**, the last item in the ROM list, to open the application's file browser. It starts
in the user's home directory and shows directories and `.nes` files only. Use
the root-directory or parent-directory button to navigate, tap a directory to
open it, or cancel to return to the library.

Tap a `.nes` file to import it. The application checks its iNES header and copies
it, with its original filename, into `$HOME/nes/rom`, creating that directory when
needed. The source file stays in place. An existing destination filename is an
error and is never overwritten. Invalid files or filesystem errors leave the
browser open with an error message; a successful import returns to the refreshed
library. Import also reports a full library and refuses to copy when `$HOME/nes/rom`
already contains 128 `.nes` files. Import is unavailable while a game is running
or stopping.

Only the ROM is copied. Matching `.sav` and `.state` files are not imported;
see [Battery RAM and Save States](#battery-ram-and-save-states) to move an existing
battery save manually.

### Formats and Compatibility

The scanner accepts regular files with a `.nes` extension, without case
sensitivity. ZIP archives and subdirectories are not opened. Titles come from
filenames with the extension removed and underscores or hyphens replaced by
spaces.

The library checks for a complete 16-byte header beginning with the iNES magic
bytes `NES\x1a`. It reads the mapper number, PRG and CHR sizes, PAL/NTSC flag,
mirroring, battery flag, and trainer flag, and computes CRC32 for valid headers.
An invalid header in a file placed directly in `$HOME/nes/rom` remains visible in the
library, but starting it reports `Invalid iNES ROM`. The import action rejects
these files before copying them.

Passing this check does not prove that the ROM payload is complete or that its
mapper is supported by FCEUmm. The emulator can load the
iNES and NES 2.0 games supported by its upstream implementation. The library
parser still uses classic iNES fields, so extended NES 2.0 mapper numbers, sizes
and region metadata may be incomplete in the list. The library does not scan
`.fds` files or provide a disk/BIOS workflow.

Before loading, the FCEUmm adapter rejects ROM files larger than 64 MiB and checks
that the file contains the declared PRG, CHR and optional 512-byte trainer. This
includes NES 2.0 extended sizes and exponent/multiplier size encoding. A ROM may
pass the library's basic header check but still be rejected here as truncated
or oversized.

FCEUmm uses automatic region detection and the core-reported frame rate after
loading, including PAL and NTSC timing. For classic iNES, upstream PAL detection
primarily uses filename tags such as `(E)`, `(Europe)` and `(PAL)`; the iNES byte 9
flag alone is not enough. NES 2.0 uses the region field in header byte 12.
Check the `fceumm: ... Hz` startup log to verify the selected rate, especially
for renamed PAL ROMs. The PAL/NTSC label in the library is only parsed metadata.
Compatibility depends on FCEUmm and the individual ROM; see
[FCEUmm Integration](05-fceumm.md).

### Opening, Refreshing, and Deleting

Tap a game row, including its icon or title, to start the emulator. Long titles
wrap. The game uses the full page height without an application title bar or
`Running <title>` status row. The screen provides a D-pad, A, B, Select, Start,
and a stop button. **Save** and **Load** share a control row with Select, Start
and Stop for the current game's manual save state. A is diagonally above and to
the right of B at 45 degrees. Stop
closes the current game and returns to the library; it does not pause the game.
The Refresh button in the bottom toolbar rescans `$HOME/nes/rom`.
There is no background filesystem watcher.

Swipe a row left to reveal its delete button. Swipe right to hide it. Tapping
delete removes the actual `.nes` file from disk immediately, without a confirmation
dialog or a trash/recycle step. It does not just remove a library entry. Associated
`.sav` and `.state` files in `$HOME/nes/states` are left in place. Deletion is
unavailable while a game is running or stopping; a filesystem error is shown in an alert.

### In-App Help

Tap Help in the bottom toolbar to read the library's actual ROM and save directory paths,
how to import an uncompressed `.nes` file, and how to refresh after manually
copying files. The page also explains tapping a game to start, swiping left
and tapping Delete to remove the disk file immediately, editing P1 keyboard
mappings, using Save / Load, and using the on-screen game controls and Stop. Help content scrolls
independently from the library; Back returns to the game list.

## Environment and Data Paths

Set environment variables in the process that launches the application. A shell
override applies to that launch; it does not reconfigure a separately running
Launcher session.

| Variable | Purpose | Default |
| --- | --- | --- |
| `HOME` | Sets the user's home directory; ROMs use `nes/rom` and saves use `nes/states` | Must be supplied as a nonempty absolute path by the launching environment |
| `NES_TOUCH_DEVICE` | Selects the Linux direct multitouch input node | `/dev/input/by-path/platform-3f205000.i2c-event` |
| `SDL_AUDIODRIVER` | Selects an SDL audio backend in the host simulator | SDL's platform default; `dummy` runs silently |

An empty `NES_TOUCH_DEVICE` value does not override its default.

```sh
./build/host-simulator/lilygo-ui-nes
```

Build prerequisites and LPM commands are documented in the
[README](../README.md). The application itself does not require an internet
connection to scan or play local ROMs.

### Battery RAM and Save States

All saves are stored in `$HOME/nes/states`, using the complete ROM filename,
including its extension. For example:

| File | Path for `HOME=/root` |
| --- | --- |
| ROM | `/root/nes/rom/game.nes` |
| Manual save state | `/root/nes/states/game.nes.state` |
| Battery RAM | `/root/nes/states/game.nes.sav` |

For ROM filenames longer than 249 bytes, save filenames use a shortened prefix
and a stable filename hash so the added extension fits the filesystem limit.

The save directory is created when needed and must be writable. An invalid
`HOME` does not fall back to a different save location.

While a game is running, tap **Save** beside Select/Start to store its current
state, or **Load** to restore the last saved state for that game. There is one
manual state per ROM; saving again replaces it. Loading is explicit and is not
performed automatically when a game starts. This also works for games without
battery-backed RAM. The top status strip shows operation progress and the result; both
buttons are disabled while an operation is pending.

FCEUmm serializes and restores the state on the emulation thread between frames.
Saving writes a temporary file and atomically replaces the previous state only
after a successful write. Loading checks the state format, checksum and ROM
identity before restoring it. A missing, corrupt or incompatible state reports
an error; ordinary save/load failures leave the game running. State files are
specific to this application's FCEUmm integration; compatibility with other
emulators or future core versions is not guaranteed.

Battery RAM is separate from the manual state. The core reads the `.sav` file
when loading the ROM and writes it when unloaded, including a normal Stop action.

FCEUmm uses the raw save-memory region and size reported by the loaded core;
this is not always 8 KiB. It writes through a temporary file and atomic rename,
and preserves an existing unreadable or wrongly sized save instead of
overwriting it. Diagnostics go to standard error. When importing saves, a
matching filename does not guarantee compatible size or mapper-specific contents.

Stop the game before backing up or replacing its `.sav` file. A forced process
termination or power loss may prevent current battery RAM from being written.
Only games that use battery-backed RAM can persist progress this way.
To keep battery progress from the former directory layout, stop the game and
copy its old save to the new name. For example, for `game.nes`:

```sh
mkdir -p "$HOME/nes/states"
cp -i "$HOME/nes/game.sav" "$HOME/nes/states/game.nes.sav"
```

For saves from other locations, use the same complete ROM filename plus `.sav`.
When the new battery-save file does not exist, the core can also load a legacy
save beside the ROM with its extension replaced by `.sav` (for example,
`$HOME/nes/rom/game.sav`). It preserves that source and writes the new save to
`$HOME/nes/states/game.nes.sav` on normal Stop. It does not search the former
`$HOME/nes/game.sav` location after the ROM has moved to `nes/rom`.
The ROM importer does not copy saves. Back up or replace manual `.state` files
while the game is stopped as well. Deleting the ROM preserves both save files.

The application provides one manual state per ROM, without a slot selector,
rewind, pause, or reset command.

The ROM library settings page provides editable player 1 keyboard mappings. Tap
an action row and press the replacement key; the change is saved immediately.
The default mapping is W/S/A/D for directions, K/J for A/B, I/U for rapid-fire
A/B, and 1/2 for Start/Select. A key already assigned to another action is
rejected. Player 2 is not supported. The configuration file is
`$XDG_CONFIG_HOME/lilygo-ui-nes/keymap.conf`, or
`$HOME/.config/lilygo-ui-nes/keymap.conf` when `XDG_CONFIG_HOME` is unset.

## Touch Input and Orientation

On Linux, the application also reads the touchscreen's evdev node directly to
track up to ten contacts. This allows a direction and A/B to remain pressed
together. Direct contacts and normal AppKit pointer events are combined before
being sent to NES controller 1.

If the direct input node cannot be opened, normal AppKit pointer input remains
available. That fallback and the desktop mouse provide single-pointer control;
simultaneous touch combinations require a working multitouch node. The library
settings page shows the player 1 keyboard mapping. While a game is running,
`W/S/A/D` control up/down/left/right, `K/J` control A/B, `I/U` provide rapid-fire
A/B, and `1/2` control Start/Select. Player 2 and physical gamepads are not
supported. Rapid-fire keys alternate their corresponding NES button at a fixed
cadence while held; touch controls and their keyboard equivalents can be used
together.

For a board with a different touch node, configure both readers consistently:

```sh
NES_TOUCH_DEVICE=/dev/input/eventX \
  lilygo-ui-nes --input /dev/input/eventX
```

Replace `eventX` with the actual touchscreen node and ensure the launching user
can read it. `NES_TOUCH_DEVICE` changes the direct reader only; AppKit's ordinary
pointer reader uses `--input`. The direct reader uses the device's reported axis
maxima and the CM0 portrait/90-degree-landscape mapping, so other panel mounting
directions may require code changes rather than a path override alone.

AppKit accepts `--orientation portrait` and `--orientation landscape`. The
simulator defaults to a `568 x 1232` logical portrait display; landscape swaps
the logical dimensions:

```sh
./build/host-simulator/lilygo-ui-nes --orientation landscape
```

The view rearranges its controls for the available space and scales the native
`256 x 240` RGB565 frame proportionally.

New frames are prepared as nearest-neighbor XRGB8888 images at the displayed size.
LVGL copies these images without its general image transform path. The conversion
buffer is reused and needs about 1 MiB at the largest current layout. Changing
orientation rebuilds the sampling tables without restarting the game.

The emulator sleeps until absolute monotonic frame deadlines. Automatic catch-up
is limited to two emulated frames per batch, with the last frame always displayed.
The UI checks for new frames every 16 ms and requests an immediate refresh when a
frame arrives. These changes do not guarantee 60 FPS on the device: landscape DRM
still redraws and rotates the complete output, and device audio still uses `aplay`.

Optional host benchmarks separate LVGL drawing from frame preparation:

```sh
NES_RENDER_BENCHMARK=1 ctest --preset host-simulator -R 'nes-(frame|render)' -V
```

The render benchmark measures a fixed image through the test display, excluding
emulation, frame preparation and real DRM scanout. The scaler benchmark reports
frame preparation separately. Compare the same build type and viewport; desktop
timings are not device FPS measurements.

## Audio

FCEUmm supplies 48 kHz audio on both supported platforms. Stage timings are documented in
[Performance Diagnostics](04-performance.md).

| Build | Output | Format |
| --- | --- | --- |
| Host simulator | SDL2 default audio device | 48,000 Hz, signed 16-bit native-endian mono |
| Linux device | `aplay` through ALSA `default` | 48,000 Hz, signed 16-bit little-endian mono |

The host backend supports macOS and Linux system audio. Audio tests use SDL's
dummy and disk backends, so they do not require speakers. Initialization and
playback errors are written to standard error. If the host audio device cannot
be initialized, video emulation can continue without sound.

The device package depends on `alsa-utils` for `aplay`. The application checks
`/usr/bin/aplay` and `/bin/aplay`, then streams PCM to `default`. The current CM0
ES8389 setup uses a 19.2 MHz master clock and requires the supported 48 kHz output
path. There is no application volume or audio-device selector; configure the
system mixer and ALSA default device.

Device PCM is queued in a bounded 50 ms application buffer and written by a
separate thread through a nonblocking pipe. Slow output cannot stall the emulator;
queue overflow discards the oldest samples. The kernel pipe and ALSA have their
own buffering, so 50 ms is not a total audio-latency guarantee. Shutdown wakes the
writer and reaps the player even when the player stops reading.

`NES_AUDIO=0` disables output while retaining synthesis for comparison.
`NES_AUDIO_PLAYER=/absolute/path` overrides the device's aplay executable for
diagnostics. `NES_PERF=1` logs audio-write time and dropped samples.

### Troubleshooting

| Symptom | Check |
| --- | --- |
| Empty ROM list | Check the launching user's `$HOME/nes/rom`, read permissions, `.nes` extension, nonrecursive scan, and 128-file limit, then refresh or import with **Add ROM**. |
| Import fails | Check the iNES header, source read permissions, destination write permissions, and whether `$HOME/nes/rom` already contains that filename or 128 `.nes` files. Stop any running game first. |
| `Invalid iNES ROM` | Use an uncompressed ROM with a complete iNES header. Renaming an archive to `.nes` does not convert it. |
| Game fails after its header is accepted | Check application standard error for FCEUmm's messages; verify the ROM payload and mapper compatibility. |
| `Could not start emulator` | Check application standard error and whether the application can start its worker thread. |
| Delete fails | Stop the game and check permissions on the ROM directory and whether the file still exists. |
| Battery progress is not restored | Check the ROM's battery flag and `$HOME/nes/states/<complete-ROM-filename>.sav`; verify write access and close the game normally. Migrate older `.sav` files to the new name if needed. |
| Save or Load fails | Read the status-strip error. Check write/read access to `$HOME/nes/states` and whether that ROM has a `.state` file; corrupt files and states from a different ROM or incompatible core are rejected. |
| Direction and A/B cannot be held together | Check `NES_TOUCH_DEVICE`, evdev access, and multitouch support; a mouse or pointer fallback has only one contact. |
| Host audio is silent | Check system output and volume, remove any `SDL_AUDIODRIVER=dummy` override, and read the application's audio diagnostics. |
| Device audio is silent | Confirm `alsa-utils` is installed, then inspect ALSA output and mixer settings below. |

Close the running game before testing the device audio path, so it releases its
audio output:

```sh
aplay -l
amixer -c 0 scontents
speaker-test -D default -c 1 -r 48000 -F S16_LE -t sine -l 1
sudo dmesg | tail -n 80
```

Use the card index reported by `aplay -l` if it differs from `0`. Audio output
errors appear on application standard error; codec clock and sample-rate errors
can also appear in the kernel log.
