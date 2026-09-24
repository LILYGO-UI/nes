#include "platform/drm_timing.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {
constexpr std::uint64_t second = 1000000000;
constexpr std::uint64_t start = 100 * second;

void complete(nes_drm_timing_t &timing, std::uint32_t sequence, std::uint64_t event_ns,
              std::uint64_t delay_ns = 80000)
{
    nes_drm_timing_complete(&timing, sequence, event_ns / second, (event_ns % second) / 1000,
                            event_ns + delay_ns);
}

void test_refresh_rate()
{
    assert(nes_drm_mode_refresh_hz(148500, 2200, 1125, false, false, 0) == 60.0);
    const double ntsc = nes_drm_mode_refresh_hz(148352, 2200, 1125, false, false, 1);
    assert(ntsc > 59.94 && ntsc < 59.941);
    assert(nes_drm_mode_refresh_hz(74250, 2200, 1125, true, false, 1) == 60.0);
    assert(nes_drm_mode_refresh_hz(148500, 2200, 1125, false, true, 1) == 30.0);
    assert(nes_drm_mode_refresh_hz(148500, 2200, 1125, false, false, 2) == 30.0);
    assert(nes_drm_mode_refresh_hz(148500, 0, 1125, false, false, 1) == 0.0);
    assert(nes_drm_mode_refresh_hz(0, 2200, 1125, false, false, 1) == 0.0);
}

void test_regular_cadence(unsigned vblank_step)
{
    nes_drm_timing_t timing{};
    nes_drm_timing_init(&timing, true, true, start);
    const unsigned frames = 60 / vblank_step;
    const std::uint64_t interval_ns = 16667000 * vblank_step;
    for (unsigned i = 1; i <= frames; ++i) {
        nes_drm_timing_submit(&timing, true, 0, 20000);
        complete(timing, i * vblank_step, start + i * interval_ns);
    }
    const auto snapshot = nes_drm_timing_snapshot(&timing, start + frames * interval_ns + 80000);
    assert(snapshot.completed == frames);
    assert(snapshot.submitted == frames);
    assert(snapshot.event_interval.count == frames - 1);
    assert(snapshot.event_interval.total_ns == (frames - 1) * interval_ns);
    assert(snapshot.event_interval.min_ns == interval_ns);
    assert(snapshot.event_interval.max_ns == interval_ns);
    assert(snapshot.receive_interval.total_ns == snapshot.event_interval.total_ns);
    assert(snapshot.delivery_delay.count == frames);
    assert(snapshot.delivery_delay.min_ns == 80000);
    assert(snapshot.delivery_delay.max_ns == 80000);
    assert(snapshot.crossed_vblanks == (frames - 1) * (vblank_step - 1));
    assert(timing.completed_total == frames);
    assert(timing.submitted_total == frames);
}

void test_stall_and_delayed_receive()
{
    nes_drm_timing_t timing{};
    nes_drm_timing_init(&timing, true, true, start);
    complete(timing, 10, start);
    complete(timing, 11, start + 16667000, 5000000);
    complete(timing, 17, start + 116667000, 10000000);
    const auto s = nes_drm_timing_snapshot(&timing, start + second);
    assert(s.crossed_vblanks == 5);
    assert(s.event_interval.count == 2);
    assert(s.event_interval.min_ns == 16667000);
    assert(s.event_interval.max_ns == 100000000);
    assert(s.receive_interval.min_ns == 21587000);
    assert(s.receive_interval.max_ns == 105000000);
    assert(s.delivery_delay.max_ns == 10000000);
}

void test_sequence_wrap_and_clock_discontinuity()
{
    nes_drm_timing_t timing{};
    nes_drm_timing_init(&timing, true, false, start);
    complete(timing, std::numeric_limits<std::uint32_t>::max(), start);
    complete(timing, 0, start + 16667000);
    auto s = nes_drm_timing_snapshot(&timing, start + second);
    assert(s.crossed_vblanks == 0);
    assert(s.sequence_resets == 0);
    assert(s.delivery_delay.count == 0);
    complete(timing, 1, start - second);
    complete(timing, 0, start);
    s = nes_drm_timing_snapshot(&timing, start + 2 * second);
    assert(s.timestamp_resets == 1);
    assert(s.sequence_resets == 1);
    assert(s.crossed_vblanks == 0);
}

void test_idle_windows_and_resume()
{
    nes_drm_timing_t timing{};
    nes_drm_timing_init(&timing, true, true, start);
    complete(timing, 10, start);
    auto s = nes_drm_timing_snapshot(&timing, start + second);
    assert(s.completed == 1);
    s = nes_drm_timing_snapshot(&timing, start + 2 * second);
    assert(s.completed == 0);
    assert(s.event_interval.count == 0);
    assert(s.elapsed_ns == second);
    complete(timing, 130, start + 2 * second);
    s = nes_drm_timing_snapshot(&timing, start + 3 * second);
    assert(s.completed == 1);
    assert(s.event_interval.count == 1);
    assert(s.event_interval.max_ns == 2 * second);
    assert(s.crossed_vblanks == 119);
    assert(timing.completed_total == 2);
}

void test_submission_errors_and_report_gate()
{
    nes_drm_timing_t timing{};
    nes_drm_timing_init(&timing, true, true, start);
    nes_drm_timing_submit(&timing, true, 0, 10000);
    nes_drm_timing_submit(&timing, false, 16, 30000);
    nes_drm_timing_replace_pending(&timing);
    nes_drm_timing_swap(&timing, true, 500000);
    nes_drm_timing_swap(&timing, false, 700000);
    nes_drm_timing_flip(&timing, 1000000);
    nes_drm_timing_report(&timing, start + second - 1);
    const auto s = nes_drm_timing_snapshot(&timing, start + second);
    assert(s.submitted == 1);
    assert(s.submit_errors == 1);
    assert(s.last_submit_errno == 16);
    assert(s.submit_cpu.count == 2);
    assert(s.submit_cpu.total_ns == 40000);
    assert(s.submit_cpu.min_ns == 10000);
    assert(s.submit_cpu.max_ns == 30000);
    assert(s.pending_replaced == 1);
    assert(s.swap_cpu.count == 2);
    assert(s.swap_errors == 1);
    assert(s.flip_cpu.count == 1);
    assert(timing.submitted_total == 1);
    const auto empty = nes_drm_timing_snapshot(&timing, start + 2 * second);
    assert(empty.submitted == 0 && empty.submit_errors == 0 && empty.submit_cpu.count == 0);

    nes_drm_timing_init(&timing, false, true, start);
    nes_drm_timing_submit(&timing, true, 0, 10000);
    complete(timing, 123, start);
    nes_drm_timing_replace_pending(&timing);
    nes_drm_timing_swap(&timing, false, 700000);
    nes_drm_timing_flip(&timing, 1000000);
    const auto disabled = nes_drm_timing_snapshot(&timing, start + second);
    assert(disabled.completed == 0 && disabled.submitted == 0 && disabled.pending_replaced == 0);
    assert(disabled.swap_errors == 0 && disabled.swap_cpu.count == 0 && disabled.flip_cpu.count == 0);
}
}  // namespace

int main()
{
    test_refresh_rate();
    test_regular_cadence(1);
    test_regular_cadence(2);
    test_stall_and_delayed_receive();
    test_sequence_wrap_and_clock_discontinuity();
    test_idle_windows_and_resume();
    test_submission_errors_and_report_gate();
}
