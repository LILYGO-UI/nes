#ifndef LILYGO_NES_DRM_TIMING_H
#define LILYGO_NES_DRM_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t count;
    uint64_t total_ns;
    uint64_t min_ns;
    uint64_t max_ns;
} nes_drm_sample_t;

typedef struct {
    uint64_t elapsed_ns;
    uint64_t submitted;
    uint64_t completed;
    uint64_t pending_replaced;
    uint64_t submit_errors;
    uint64_t swap_errors;
    uint64_t crossed_vblanks;
    uint64_t sequence_resets;
    uint64_t timestamp_resets;
    int last_submit_errno;
    nes_drm_sample_t event_interval;
    nes_drm_sample_t receive_interval;
    nes_drm_sample_t delivery_delay;
    nes_drm_sample_t submit_cpu;
    nes_drm_sample_t flip_cpu;
    nes_drm_sample_t swap_cpu;
} nes_drm_snapshot_t;

/* Updated and sampled only on the LVGL display thread. */
typedef struct {
    bool enabled;
    bool timestamp_monotonic;
    bool have_previous;
    uint32_t previous_sequence;
    uint64_t previous_event_ns;
    uint64_t previous_receive_ns;
    uint64_t report_ns;
    uint64_t submitted_total;
    uint64_t completed_total;
    nes_drm_snapshot_t window;
} nes_drm_timing_t;

uint64_t nes_drm_now_ns(void);
double nes_drm_mode_refresh_hz(uint32_t clock_khz, uint32_t htotal, uint32_t vtotal,
                               bool interlaced, bool doublescan, uint32_t vscan);
void nes_drm_timing_init(nes_drm_timing_t *timing, bool enabled, bool timestamp_monotonic,
                         uint64_t now_ns);
void nes_drm_timing_submit(nes_drm_timing_t *timing, bool success, int error, uint64_t elapsed_ns);
void nes_drm_timing_complete(nes_drm_timing_t *timing, uint32_t sequence, uint32_t sec,
                             uint32_t usec, uint64_t received_ns);
void nes_drm_timing_replace_pending(nes_drm_timing_t *timing);
void nes_drm_timing_flip(nes_drm_timing_t *timing, uint64_t elapsed_ns);
void nes_drm_timing_swap(nes_drm_timing_t *timing, bool success, uint64_t elapsed_ns);
nes_drm_snapshot_t nes_drm_timing_snapshot(nes_drm_timing_t *timing, uint64_t now_ns);
void nes_drm_timing_report(nes_drm_timing_t *timing, uint64_t now_ns);

#ifdef __cplusplus
}
#endif
#endif
