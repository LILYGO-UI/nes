# ROMs and Runtime

[English README](../README.md) | [Chinese README](../README.zh-CN.md) | [Architecture](00-overview.md)

## ROM Library

The application runs local, user-provided ROMs. No ROM downloads or commercial
ROMs are included. Use files that you are legally entitled to run.

### ROM Search Paths

The library scans these directories in order:

| Order | Directory |
| --- | --- |
| 1 | Nonempty `NES_ROM_DIR`, when set |
| 2 | `$HOME/nes`, when `HOME` is set |
| 3 | `/root/nes` |
| 4 | `/root/roms/nes` |
| 5 | `/mnt/sdcard/nes` |
| 6 | `/mnt/nes` |
| 7 | `/media/nes` |

`NES_ROM_DIR` adds a preferred search location; the other locations remain
enabled. Missing or inaccessible directories are skipped. Scans are not
recursive, so place ROM files directly in a listed directory. The library
collects at most 128 files across all locations, then sorts them by title without
case sensitivity. When a directory contains more files than the remaining
capacity, filesystem iteration order determines which files are included.

Identical directory strings are removed from the default search list. Different
paths or symbolic links to the same directory are not resolved for deduplication,
and ROM contents are not deduplicated. The directory shown in the interface is
the first configured search path, even if it does not exist; it is not a complete
list of scanned directories.

For a custom host library:

```sh
mkdir -p "$HOME/nes"
NES_ROM_DIR="$HOME/nes" lpm start --no-build --foreground
```

Copy your ROM files into that directory before launching, or use the refresh
button after copying them.

### Formats and Compatibility

The scanner accepts regular files with a `.nes` extension, without case
sensitivity. ZIP archives and subdirectories are not opened. Titles come from
filenames with the extension removed and underscores or hyphens replaced by
spaces.

The library checks for a complete 16-byte header beginning with the iNES magic
bytes `NES\x1a`. It reads the mapper number, PRG and CHR sizes, PAL/NTSC flag,
mirroring, battery flag, and trainer flag, and computes CRC32 for valid headers.
An invalid header remains visible in the library, but starting it reports
`Invalid iNES ROM`.

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
wrap. The screen provides a D-pad, A, B, Select, Start, and a stop button. Stop
closes the current game and returns to the library; it does not pause the game.
The refresh button is available in the library and rescans the configured paths.
There is no background filesystem watcher.

Swipe a row left to reveal its delete button. Swipe right to hide it. Tapping
delete removes the actual `.nes` file from disk immediately, without a confirmation
dialog or a trash/recycle step. It does not just remove a library entry. Associated
`.sav` files are left in place. Deletion is unavailable while a game is running or
stopping; a filesystem error is shown in the status text.

## Environment and Data Paths

Set environment variables in the process that launches the application. A shell
override applies to that launch; it does not reconfigure a separately running
Launcher session.

| Variable | Purpose | Default |
| --- | --- | --- |
| `NES_ROM_DIR` | Adds the first ROM search directory | The remaining search list above |
| `NES_TOUCH_DEVICE` | Selects the Linux direct multitouch input node | `/dev/input/by-path/platform-3f205000.i2c-event` |
| `SDL_AUDIODRIVER` | Selects an SDL audio backend in the host simulator | SDL's platform default; `dummy` runs silently |

Empty `NES_ROM_DIR` and `NES_TOUCH_DEVICE` values do not override their defaults.

```sh
NES_ROM_DIR="$HOME/nes" \
  ./build/host-simulator/lilygo-ui-nes
```

Build prerequisites and LPM commands are documented in the
[README](../README.md). The application itself does not require an internet
connection to scan or play local ROMs.

### Battery RAM and Save States

Battery RAM is stored beside the ROM, replacing its extension with `.sav`. For
example, `/root/nes/game.nes` uses `/root/nes/game.sav`. The core reads it when
loading the ROM and writes it when unloaded, including a normal Stop action.
The ROM directory must be writable.

FCEUmm uses the raw save-memory region and size reported by the loaded core;
this is not always 8 KiB. It writes through a temporary file and atomic rename,
and preserves an existing unreadable or wrongly sized save instead of
overwriting it. Diagnostics go to standard error. When importing saves, a
matching filename does not guarantee compatible size or mapper-specific contents.

Stop the game before backing up or replacing its `.sav` file. A forced process
termination or power loss may prevent current battery RAM from being written.
Only games that use battery-backed RAM can persist progress this way.

The application exposes no quick-save, save-state slots, load-state controls,
rewind, pause, or reset command. State-file support in the vendored core is not
connected to the current UI.

## Touch Input and Orientation

On Linux, the application also reads the touchscreen's evdev node directly to
track up to ten contacts. This allows a direction and A/B to remain pressed
together. Direct contacts and normal AppKit pointer events are combined before
being sent to NES controller 1.

If the direct input node cannot be opened, normal AppKit pointer input remains
available. That fallback and the desktop mouse provide single-pointer control;
simultaneous touch combinations require a working multitouch node. There is no
application keyboard mapping, physical gamepad integration, or second-player
control mapping.

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
| `No ROM files found` | Check the search paths, read permissions, `.nes` extension, nonrecursive scan, and 128-file limit, then refresh. |
| `Invalid iNES ROM` | Use an uncompressed ROM with a complete iNES header. Renaming an archive to `.nes` does not convert it. |
| Game fails after its header is accepted | Check application standard error for FCEUmm's messages; verify the ROM payload and mapper compatibility. |
| `Could not start emulator` | Check application standard error and whether the application can start its worker thread. |
| Delete fails | Stop the game and check permissions on the ROM directory and whether the file still exists. |
| Progress is not restored | Check the ROM's battery flag and the matching `.sav` file beside it; verify write access and close the game normally. |
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
