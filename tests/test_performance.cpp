#include "emulator/nes_performance.hpp"

#include <cassert>
#include <atomic>
#include <cstdlib>
#include <thread>

void test_concurrent_snapshots()
{
    nes_performance::start("concurrent");
    constexpr std::uint64_t samples_per_worker = 10000;
    constexpr std::uint64_t duration_ns = 700;
    std::atomic<bool> begin{false};
    std::atomic<unsigned> remaining{2};
    const auto record_frames = [&] {
        while (!begin.load(std::memory_order_acquire)) std::this_thread::yield();
        for (std::uint64_t index = 0; index < samples_per_worker; ++index) {
            nes_performance::record(nes_performance::Stage::emulation, duration_ns);
            nes_performance::record_audio_dropped(3);
            nes_performance::record_audio_generated(800);
            nes_performance::record_audio_output(799);
        }
        remaining.fetch_sub(1, std::memory_order_release);
    };
    std::thread first(record_frames);
    std::thread second(record_frames);
    std::uint64_t collected_samples = 0;
    std::uint64_t dropped_samples = 0;
    std::uint64_t generated_samples = 0;
    std::uint64_t output_samples = 0;
    const auto collect = [&] {
        const auto snapshot = nes_performance::take_snapshot();
        const auto &sample = snapshot.stages[static_cast<std::size_t>(nes_performance::Stage::emulation)];
        assert(sample.total_ns == sample.count * duration_ns);
        assert(sample.max_ns == (sample.count ? duration_ns : 0));
        collected_samples += sample.count;
        dropped_samples += snapshot.audio_dropped_samples;
        generated_samples += snapshot.audio_generated_samples;
        output_samples += snapshot.audio_output_samples;
    };
    begin.store(true, std::memory_order_release);
    do {
        collect();
    } while (remaining.load(std::memory_order_acquire));
    first.join();
    second.join();
    collect();
    assert(collected_samples == 2 * samples_per_worker);
    assert(dropped_samples == 6 * samples_per_worker);
    assert(generated_samples == 1600 * samples_per_worker);
    assert(output_samples == 1598 * samples_per_worker);
}

int main()
{
    unsetenv("NES_PERF");
    nes_performance::start("disabled");
    assert(!nes_performance::enabled());
    nes_performance::record(nes_performance::Stage::emulation, 100);
    nes_performance::record_audio_dropped(123);
    nes_performance::record_audio_generated(800);
    nes_performance::record_audio_output(800);
    const auto disabled_snapshot = nes_performance::take_snapshot();
    assert(disabled_snapshot.stages[0].count == 0);
    assert(disabled_snapshot.audio_dropped_samples == 0);
    assert(disabled_snapshot.audio_generated_samples == 0);
    assert(disabled_snapshot.audio_output_samples == 0);

    setenv("NES_PERF", "1", 1);
    nes_performance::start("test");
    assert(nes_performance::enabled());
    const auto record_frames = [] {
        for (unsigned index = 1; index <= 1000; ++index) {
            nes_performance::record(nes_performance::Stage::emulation, index);
        }
    };
    std::thread worker(record_frames);
    record_frames();
    worker.join();
    nes_performance::record(nes_performance::Stage::gpu_upload, 9000);
    nes_performance::record(nes_performance::Stage::count, 123);
    nes_performance::record_audio_dropped(700);
    nes_performance::record_audio_dropped(400);
    nes_performance::record_audio_generated(800);
    nes_performance::record_audio_output(400);
    const auto snapshot = nes_performance::take_snapshot();
    const auto &emulation = snapshot.stages[static_cast<std::size_t>(nes_performance::Stage::emulation)];
    assert(emulation.count == 2000);
    assert(emulation.total_ns == 1001000);
    assert(emulation.max_ns == 1000);
    const auto &gpu = snapshot.stages[static_cast<std::size_t>(nes_performance::Stage::gpu_upload)];
    assert(gpu.count == 1);
    assert(gpu.total_ns == 9000);
    assert(gpu.max_ns == 9000);
    assert(snapshot.audio_dropped_samples == 1100);
    assert(snapshot.audio_generated_samples == 800);
    assert(snapshot.audio_output_samples == 400);
    const auto empty_snapshot = nes_performance::take_snapshot();
    assert(empty_snapshot.audio_dropped_samples == 0);
    assert(empty_snapshot.audio_generated_samples == 0);
    assert(empty_snapshot.audio_output_samples == 0);
    for (const auto &sample : empty_snapshot.stages) {
        assert(sample.count == 0);
        assert(sample.total_ns == 0);
        assert(sample.max_ns == 0);
    }
    test_concurrent_snapshots();

    nes_performance::record(nes_performance::Stage::frame_copy, 400);
    nes_performance::record_audio_dropped(400);
    nes_performance::record_audio_generated(800);
    nes_performance::record_audio_output(799);
    setenv("NES_PERF", "0", 1);
    nes_performance::start("disabled");
    assert(!nes_performance::enabled());
    const auto reset_snapshot = nes_performance::take_snapshot();
    assert(reset_snapshot.audio_dropped_samples == 0);
    assert(reset_snapshot.audio_generated_samples == 0);
    assert(reset_snapshot.audio_output_samples == 0);
    for (const auto &sample : reset_snapshot.stages) assert(sample.count == 0);
    unsetenv("NES_PERF");
}
