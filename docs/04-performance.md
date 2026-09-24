# Performance Diagnostics

## On-screen LVGL Monitors

The LVGL performance and memory overlays are enabled by default in both the
software and GLES builds, independently of `NES_PERF`. They appear in the ROM
library and during gameplay and update approximately every 300 ms:

- Bottom right: LVGL FPS, CPU percentage and `total ms (render | flush)`.
- Bottom left: LVGL heap usage in kB and percent, peak usage and fragmentation.

`CPU` uses `100 - lv_timer_get_idle()`: the fraction of elapsed time spent inside
LVGL's timer handler, including synchronous display waits. It is not Linux
system/process CPU usage and does not measure the separate NES and audio
threads. The idle estimate updates every 500 ms; ignore the initial readings.
The FPS counter describes LVGL refreshes, not completed DRM flips or the panel's
physical refresh rate.

The memory monitor measures the built-in 8 MiB LVGL heap. It excludes allocations
outside that heap, including the NES core's C/C++ allocations, external font
allocations, driver buffers and GPU textures; it is not the process RSS.

Monitoring itself adds some redraw and heap-inspection work. For comparisons
without this overhead, disable the overlays at configure time and rebuild:

```sh
cmake --preset cm0-cross-gles -DLILYGO_NES_LVGL_MONITOR=OFF
cmake --build --preset cm0-cross-gles --parallel
```

Use `-DLILYGO_NES_LVGL_MONITOR=ON` to enable them again.

## Stage Timing Logs

For a standalone device test, release the Launcher's DRM ownership first. Enable
measurements with `sudo env` so the variables reach the root application:

```sh
sudo systemctl stop lilygo-ui-launcher.service
sudo env NES_PERF=1 lilygo-ui-nes --display drm --orientation portrait 2>&1 | tee nes-perf.log
```

Exit NES before restoring the launcher:

```sh
sudo systemctl start lilygo-ui-launcher.service
```

The host simulator accepts the same variable:

```sh
NES_PERF=1 ./build/host-simulator/lilygo-ui-nes 2>nes-perf.log
```

Choose a ROM normally. Each game start resets the counters. The default is off;
only the exact value `1` enables measurements. With measurements disabled, the
instrumented operations do not read the clock or update timing counters. Logs
go to standard error, not the game interface.

Each `nes perf:` line identifies the core and the actual elapsed reporting
interval. Each stage is printed as `count(rate/s,average/maxms)`, for example
`emu=60(59.8/s,3.100/4.250ms)`. Rates use elapsed monotonic time rather than
assuming that the UI timer ran exactly once per second.

| Field | Measured operation |
| --- | --- |
| `emu` | One FCEUmm emulation frame, excluding frame-clock sleep. It measures the complete `retro_run`, including internal APU work and its video/audio callbacks. |
| `video` | Conversion/publication of a native frame for the UI. Its rate indicates generated video frames. |
| `copy` | Copying a new native frame from the emulator to the UI buffer after acquiring the frame lock; lock wait time is excluded. A sustained lower rate than `video` means the UI did not consume every generated frame. |
| `gpu` | Native-frame texture update on the UI thread, including any channel conversion and GL submission. |
| `scale` | Software frame preparation for the LVGL image; zero in the GPU build. |
| `audio_gen` | FCEUmm's audio callback processing. Internal APU work is counted in `emu`. |
| `audio_write` | Submitting audio to the host output queue or completing a PCM block write to device audio, including output backpressure. |

FCEUmm's `video` and `audio_gen` measurements are nested within `emu`. Do not add
them to `emu` when estimating its total frame cost.

The additional `audio_drop_samples` field counts PCM samples discarded when the
device output queue overruns. Divide by the active audio sample rate to convert
that count to lost playback time. A sustained nonzero value means audio output
cannot keep up even when it no longer blocks emulation.

`pcm_gen=count(samples/s)` counts samples delivered by the core. `pcm_out` counts
samples successfully queued to SDL or complete PCM blocks written to the device
player. Neither measures DAC playback. Compare several reporting windows because
generation and output use separate threads and whole-frame/block counters. At
48 kHz, an exact 60 Hz clock requires 800 samples per frame; 22.05 kHz alternates
367 and 368. FCEUmm uses its core-reported regional frame rate and audio cadence,
so its per-frame sample counts can differ. Compare the long-term sample rate
against the configured output rate. `NES_AUDIO=0` retains `pcm_gen` but makes
`pcm_out` zero.

CPU scopes use `std::chrono::steady_clock`. Each stage uses a short mutex to
collect and reset its count, total and maximum together, so concurrent core,
audio and LVGL work cannot split a sample across reporting windows. A sample is
recorded when its operation completes. The reporting timer runs only on the
LVGL thread, so a stalled UI can produce an interval longer than one second.

The `gpu` number is CPU submission time, not GPU execution time. No GPU fence is
inserted for measurement. `copy` and `gpu` rates describe submitted frames; they
are not DRM page-flip or physical display refresh measurements. A generated
frame can also be intentionally skipped by a core's catch-up policy, so compare
`emu`, `video` and `copy` rates together.

## DRM Presentation

In the device GLES build, `NES_PERF=1` also enables DRM diagnostics. The startup
`nes drm mode:` line identifies the device, connector, CRTC and selected mode.
`refresh_hz` is computed from the mode's pixel clock and totals, including its
scan flags; it is the nominal mode refresh rate, not a measured game frame rate.
`timestamp_clock` reports the kernel's DRM timestamp clock capability.

The per-second `nes drm:` line uses completed DRM page-flip events, rather than
texture uploads. Counters are per display session and include ordinary UI updates:

| Field | Meaning |
| --- | --- |
| `submitted`, `completed` | Successful page-flip requests and completion callbacks received during the window. The initial CRTC modeset is excluded. |
| `callback_hz` | Completion callbacks received per second of monotonic wall time. |
| `kernel_hz` | Inverse mean interval between successive kernel page-flip timestamps, including intervals crossing reporting windows. |
| `total_submit`, `total_complete` | Cumulative page-flip request/completion counts since the display opened. |
| `pending_replaced` | Frames replaced in the driver's pending slot before submission to KMS. |
| `crossed_vblanks` | Additional vblank sequence numbers between consecutive completion events. This is not a count of lost game frames. |
| `kernel_dt` | Interval between kernel page-flip timestamps. |
| `receive_dt` | Interval between userspace receiving completion callbacks. |
| `delivery` | Callback receipt time minus kernel timestamp, available only when DRM timestamps are monotonic. |
| `submit_cpu` | CPU wall time spent in `drmModePageFlip`. |
| `flip_cpu` | Entire DRM submission callback, including GBM operations and existing waits. |
| `swap_cpu` | CPU wall time spent in `eglSwapBuffers`, not GPU execution time. |
| `submit_errors`, `errno`, `swap_errors` | Failed DRM submissions, last submission error number, and failed EGL swaps in the window. |
| `sequence_resets`, `timestamp_resets` | Sequence or timestamp discontinuities excluded from the corresponding normal interval calculation. |

DRM duration fields use `count(min/average/maxms)`, unlike the two-duration
`nes perf:` fields. No observations produce zero counts and zero durations.
The diagnostic timer reports idle windows too. Event processing still follows
the existing rendering path, so an idle display can deliver its last completion
late. Long idle intervals and crossed vblanks are expected in the ROM library.
Judge cadence during continuous gameplay, not startup, menus or after stopping.

For a nominal 60 Hz mode, consistent `kernel_dt` near 16.67 ms indicates regular
page flips. Values near 33.3/50 ms during continuous gameplay indicate updates
spanning two/three refresh intervals; compare them with generation rates and
`pending_replaced`. These measurements do not change the existing EGL swap
interval or nonblocking DRM submission policy, and they cannot identify whether
successive displayed frames contain different game pixels.

## Compare on the Device

Use the same ROM, scene, orientation, audio configuration and build type for
each core. Run past startup, then record at least two minutes of normal play when
checking audio drift and presentation cadence.
Keep the original logs so average throughput and occasional maximum spikes can
be compared separately.

At 60 Hz the total per-frame CPU budget is about 16.7 ms; at 50 Hz it is 20 ms.
For FCEUmm, `emu` already includes its audio and video callbacks. For the older
cores, inspect `emu`, `audio_gen` and `video` together. Treat these as approximate
frame-cost comparisons because the measurement boundaries differ. A core frame
cost near its budget leaves little time for other work. A healthy emulation rate
with lower `video` or `copy` rates points to a different
part of the pipeline. Long `audio_write` times indicate output backpressure;
with the device writer running separately, those times no longer directly block
the emulation thread. Short GPU submission times do not prove that the display
is scanning out smoothly; DRM and real device measurements are still needed.

The counter unit test verifies disabled collection, concurrent accumulation and
consistent snapshots, maximum values and reset behavior without depending on
wall-clock timings:

```sh
ctest --preset host-simulator -R nes-performance --output-on-failure
```
