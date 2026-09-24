#ifndef LILYGO_UI_NES_PAGES_NES_FRAME_SCALER_HPP
#define LILYGO_UI_NES_PAGES_NES_FRAME_SCALER_HPP

#include "emulator/nes_emulator.hpp"

#include <array>
#include <cstdint>
#include <vector>

class NesFrameScaler {
public:
    bool resize(int width, int height);
    void update(const std::array<std::uint16_t, NesEmulator::frame_pixels> &source) noexcept;

    int width() const noexcept
    {
        return width_;
    }
    int height() const noexcept
    {
        return height_;
    }
    const std::vector<std::uint32_t> &pixels() const noexcept
    {
        return pixels_;
    }

private:
    int width_  = 0;
    int height_ = 0;
    std::vector<int> columns_;
    std::vector<int> rows_;
    std::vector<std::uint32_t> pixels_;
};

#endif
