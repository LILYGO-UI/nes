#include "emulator/nes_performance.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace nes_performance {
namespace {

using Clock = std::chrono::steady_clock;

struct Counter {
    std::mutex mutex;
    Sample sample;
};

std::array<Counter, static_cast<std::size_t>(Stage::count)> counters{};
std::atomic<std::uint64_t> audio_dropped_samples{0};
std::atomic<std::uint64_t> audio_generated_samples{0};
std::atomic<std::uint64_t> audio_output_samples{0};
std::atomic<bool> active{false};
std::string core_name;
Clock::time_point last_report{};

}  // namespace

void start(const char *core)
{
    active.store(false, std::memory_order_relaxed);
    take_snapshot();
    core_name   = core ? core : "unknown";
    last_report = Clock::now();
    const char *setting = std::getenv("NES_PERF");
    active.store(setting && std::strcmp(setting, "1") == 0, std::memory_order_relaxed);
}

bool enabled() noexcept
{
    return active.load(std::memory_order_relaxed);
}

void record(Stage stage, std::uint64_t elapsed_ns) noexcept
{
    const auto index = static_cast<std::size_t>(stage);
    if (!enabled() || index >= counters.size()) return;
    auto &counter = counters[index];
    std::lock_guard<std::mutex> lock(counter.mutex);
    ++counter.sample.count;
    counter.sample.total_ns += elapsed_ns;
    if (counter.sample.max_ns < elapsed_ns) counter.sample.max_ns = elapsed_ns;
}

Snapshot take_snapshot() noexcept
{
    Snapshot snapshot{};
    for (std::size_t index = 0; index < counters.size(); ++index) {
        auto &counter = counters[index];
        std::lock_guard<std::mutex> lock(counter.mutex);
        snapshot.stages[index] = counter.sample;
        counter.sample = {};
    }
    snapshot.audio_dropped_samples = audio_dropped_samples.exchange(0, std::memory_order_relaxed);
    snapshot.audio_generated_samples = audio_generated_samples.exchange(0, std::memory_order_relaxed);
    snapshot.audio_output_samples = audio_output_samples.exchange(0, std::memory_order_relaxed);
    return snapshot;
}

void record_audio_dropped(std::uint64_t samples) noexcept
{
    if (enabled()) audio_dropped_samples.fetch_add(samples, std::memory_order_relaxed);
}

void record_audio_generated(std::uint64_t samples) noexcept
{
    if (enabled()) audio_generated_samples.fetch_add(samples, std::memory_order_relaxed);
}

void record_audio_output(std::uint64_t samples) noexcept
{
    if (enabled()) audio_output_samples.fetch_add(samples, std::memory_order_relaxed);
}

void report_if_due() noexcept
{
    if (!enabled()) return;
    const auto now = Clock::now();
    const double interval = std::chrono::duration<double>(now - last_report).count();
    if (interval < 1.0) return;
    last_report = now;
    const auto snapshot = take_snapshot();
    bool any = snapshot.audio_dropped_samples != 0 || snapshot.audio_generated_samples != 0 ||
               snapshot.audio_output_samples != 0;
    for (const auto &sample : snapshot.stages) any = any || sample.count != 0;
    if (!any) return;

    static constexpr const char *names[] = {"emu", "video", "copy", "gpu", "scale", "audio_gen", "audio_write"};
    std::fprintf(stderr, "nes perf: core=%s interval=%.3fs", core_name.c_str(), interval);
    for (std::size_t index = 0; index < snapshot.stages.size(); ++index) {
        const auto &sample = snapshot.stages[index];
        const double average_ms = sample.count ? static_cast<double>(sample.total_ns) / sample.count / 1e6 : 0.0;
        std::fprintf(stderr, " %s=%llu(%.1f/s,%.3f/%.3fms)", names[index],
                     static_cast<unsigned long long>(sample.count), sample.count / interval, average_ms,
                     static_cast<double>(sample.max_ns) / 1e6);
    }
    std::fprintf(stderr, " pcm_gen=%llu(%.1f/s) pcm_out=%llu(%.1f/s) audio_drop_samples=%llu\n",
                 static_cast<unsigned long long>(snapshot.audio_generated_samples),
                 snapshot.audio_generated_samples / interval,
                 static_cast<unsigned long long>(snapshot.audio_output_samples),
                 snapshot.audio_output_samples / interval,
                 static_cast<unsigned long long>(snapshot.audio_dropped_samples));
}

Scope::Scope(Stage stage) noexcept : stage_(stage), active_(enabled())
{
    if (active_) started_ = Clock::now();
}

Scope::~Scope()
{
    if (active_) {
        record(stage_, static_cast<std::uint64_t>(
                           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started_).count()));
    }
}

}  // namespace nes_performance
