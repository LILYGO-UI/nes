#ifndef LILYGO_UI_NES_PLATFORM_NES_GPU_FRAME_HPP
#define LILYGO_UI_NES_PLATFORM_NES_GPU_FRAME_HPP

#include "emulator/nes_emulator.hpp"

#include <lvgl.h>

#if LV_USE_DRAW_OPENGLES

#include <array>
#include <cstdint>

// All operations, including destruction, require the display's current GL context.
class NesGpuFrame {
public:
    NesGpuFrame() = default;
    ~NesGpuFrame();
    NesGpuFrame(const NesGpuFrame &) = delete;
    NesGpuFrame &operator=(const NesGpuFrame &) = delete;

    bool initialize() noexcept;
    bool update(const std::array<std::uint16_t, NesEmulator::frame_pixels> &source) noexcept;
    void reset() noexcept;

    unsigned int texture_id() const noexcept { return texture_id_; }
    bool ready() const noexcept { return texture_id_ != 0 && error_[0] == '\0'; }
    const char *error() const noexcept { return error_.data(); }

private:
    bool fail(const char *operation, unsigned int error) noexcept;

    unsigned int texture_id_ = 0;
    std::array<std::uint16_t, NesEmulator::frame_pixels> upload_buffer_{};
    std::array<char, 160> error_{};
};

#endif
#endif
