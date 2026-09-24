#include "drm_timing.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void record(nes_drm_sample_t *sample, uint64_t ns)
{
    if (!sample->count || ns < sample->min_ns) sample->min_ns = ns;
    if (ns > sample->max_ns) sample->max_ns = ns;
    ++sample->count;
    sample->total_ns += ns;
}

uint64_t nes_drm_now_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

double nes_drm_mode_refresh_hz(uint32_t clock_khz, uint32_t htotal, uint32_t vtotal,
                               bool interlaced, bool doublescan, uint32_t vscan)
{
    if (!clock_khz || !htotal || !vtotal) return 0.0;
    double rate = (double)clock_khz * 1000.0 / htotal / vtotal;
    if (interlaced) rate *= 2.0;
    if (doublescan) rate /= 2.0;
    if (vscan > 1) rate /= vscan;
    return rate;
}

void nes_drm_timing_init(nes_drm_timing_t *timing, bool enabled, bool timestamp_monotonic,
                         uint64_t now_ns)
{
    memset(timing, 0, sizeof(*timing));
    timing->enabled = enabled;
    timing->timestamp_monotonic = timestamp_monotonic;
    timing->report_ns = now_ns;
}

void nes_drm_timing_submit(nes_drm_timing_t *timing, bool success, int error, uint64_t elapsed_ns)
{
    if (!timing->enabled) return;
    record(&timing->window.submit_cpu, elapsed_ns);
    if (success) {
        ++timing->window.submitted;
        ++timing->submitted_total;
    } else {
        ++timing->window.submit_errors;
        timing->window.last_submit_errno = error;
    }
}

void nes_drm_timing_complete(nes_drm_timing_t *timing, uint32_t sequence, uint32_t sec,
                             uint32_t usec, uint64_t received_ns)
{
    if (!timing->enabled) return;
    ++timing->window.completed;
    ++timing->completed_total;
    const uint64_t event_ns = (uint64_t)sec * UINT64_C(1000000000) + (uint64_t)usec * 1000;
    if (timing->have_previous) {
        /* Unsigned subtraction handles the kernel's 32-bit sequence wrap. */
        const uint32_t advance = sequence - timing->previous_sequence;
        if (advance > 0 && advance < UINT32_C(0x80000000)) {
            timing->window.crossed_vblanks += advance - 1;
        } else {
            ++timing->window.sequence_resets;
        }
        if (event_ns > timing->previous_event_ns) {
            record(&timing->window.event_interval, event_ns - timing->previous_event_ns);
        } else {
            ++timing->window.timestamp_resets;
        }
        if (received_ns >= timing->previous_receive_ns) {
            record(&timing->window.receive_interval, received_ns - timing->previous_receive_ns);
        }
    }
    if (timing->timestamp_monotonic && received_ns >= event_ns) {
        record(&timing->window.delivery_delay, received_ns - event_ns);
    }
    timing->previous_sequence = sequence;
    timing->previous_event_ns = event_ns;
    timing->previous_receive_ns = received_ns;
    timing->have_previous = true;
}

void nes_drm_timing_replace_pending(nes_drm_timing_t *timing)
{
    if (timing->enabled) ++timing->window.pending_replaced;
}

void nes_drm_timing_flip(nes_drm_timing_t *timing, uint64_t elapsed_ns)
{
    if (timing->enabled) record(&timing->window.flip_cpu, elapsed_ns);
}

void nes_drm_timing_swap(nes_drm_timing_t *timing, bool success, uint64_t elapsed_ns)
{
    if (!timing->enabled) return;
    record(&timing->window.swap_cpu, elapsed_ns);
    if (!success) ++timing->window.swap_errors;
}

nes_drm_snapshot_t nes_drm_timing_snapshot(nes_drm_timing_t *timing, uint64_t now_ns)
{
    nes_drm_snapshot_t result = timing->window;
    result.elapsed_ns = now_ns >= timing->report_ns ? now_ns - timing->report_ns : 0;
    memset(&timing->window, 0, sizeof(timing->window));
    timing->report_ns = now_ns;
    return result;
}

static double average_ms(const nes_drm_sample_t *sample)
{
    return sample->count ? (double)sample->total_ns / sample->count / 1e6 : 0.0;
}

static void print_sample(const char *name, const nes_drm_sample_t *sample)
{
    fprintf(stderr, " %s=%" PRIu64 "(%.3f/%.3f/%.3fms)", name, sample->count,
            sample->min_ns / 1e6, average_ms(sample), sample->max_ns / 1e6);
}

void nes_drm_timing_report(nes_drm_timing_t *timing, uint64_t now_ns)
{
    if (!timing->enabled || now_ns < timing->report_ns ||
        now_ns - timing->report_ns < UINT64_C(1000000000)) return;
    const nes_drm_snapshot_t s = nes_drm_timing_snapshot(timing, now_ns);
    const double seconds = s.elapsed_ns / 1e9;
    const double kernel_hz = s.event_interval.total_ns ?
        (double)s.event_interval.count * 1e9 / s.event_interval.total_ns : 0.0;
    fprintf(stderr, "nes drm: interval=%.3fs submitted=%" PRIu64 " completed=%" PRIu64
            " callback_hz=%.2f kernel_hz=%.2f total_submit=%" PRIu64 " total_complete=%" PRIu64
            " pending_replaced=%" PRIu64 " crossed_vblanks=%" PRIu64
            " submit_errors=%" PRIu64 " errno=%d swap_errors=%" PRIu64
            " sequence_resets=%" PRIu64 " timestamp_resets=%" PRIu64,
            seconds, s.submitted, s.completed, s.completed / seconds, kernel_hz,
            timing->submitted_total, timing->completed_total, s.pending_replaced,
            s.crossed_vblanks, s.submit_errors, s.last_submit_errno, s.swap_errors,
            s.sequence_resets, s.timestamp_resets);
    print_sample("kernel_dt", &s.event_interval);
    print_sample("receive_dt", &s.receive_interval);
    print_sample("delivery", &s.delivery_delay);
    print_sample("submit_cpu", &s.submit_cpu);
    print_sample("flip_cpu", &s.flip_cpu);
    print_sample("swap_cpu", &s.swap_cpu);
    fputc('\n', stderr);
}
