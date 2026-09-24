#ifndef LILYGO_UI_NES_EMULATOR_NES_PERFORMANCE_HPP
#define LILYGO_UI_NES_EMULATOR_NES_PERFORMANCE_HPP

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace nes_performance {

enum class Stage : std::size_t {
    emulation,
    video_blit,
    frame_copy,
    gpu_upload,
    frame_prepare,
    audio_generate,
    audio_write,
    count,
};

struct Sample {
    std::uint64_t count    = 0;
    std::uint64_t total_ns = 0;
    std::uint64_t max_ns   = 0;
};

struct Snapshot {
    std::array<Sample, static_cast<std::size_t>(Stage::count)> stages{};
    std::uint64_t audio_dropped_samples = 0;
    std::uint64_t audio_generated_samples = 0;
    std::uint64_t audio_output_samples = 0;
};

void start(const char *core);
bool enabled() noexcept;
void record(Stage stage, std::uint64_t elapsed_ns) noexcept;
void record_audio_dropped(std::uint64_t samples) noexcept;
void record_audio_generated(std::uint64_t samples) noexcept;
// SDL queue acceptance or completed device PCM blocks, not DAC playback.
void record_audio_output(std::uint64_t samples) noexcept;
Snapshot take_snapshot() noexcept;
void report_if_due() noexcept;

class Scope {
public:
    explicit Scope(Stage stage) noexcept;
    ~Scope();
    Scope(const Scope &)            = delete;
    Scope &operator=(const Scope &) = delete;

private:
    Stage stage_;
    bool active_;
    std::chrono::steady_clock::time_point started_{};
};

}  // namespace nes_performance

#endif
