#include "pages/nes/frame_scaler.hpp"

#include <algorithm>

bool NesFrameScaler::resize(int width, int height)
{
    if (width <= 0 || height <= 0 || (width == width_ && height == height_)) return false;
    pixels_.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    columns_.resize(width);
    rows_.resize(height);
    width_  = width;
    height_ = height;
    for (int x = 0; x < width; ++x) columns_[x] = x * NesEmulator::frame_width / width;
    for (int y = 0; y < height; ++y) rows_[y] = y * NesEmulator::frame_height / height;
    return true;
}

void NesFrameScaler::update(const std::array<std::uint16_t, NesEmulator::frame_pixels> &source) noexcept
{
    for (int y = 0; y < height_; ++y) {
        auto *destination = pixels_.data() + static_cast<std::size_t>(y) * width_;
        // Upscaling repeats source rows; reuse the already converted row.
        if (y > 0 && rows_[y] == rows_[y - 1]) {
            std::copy_n(destination - width_, width_, destination);
            continue;
        }
        const auto *row = source.data() + static_cast<std::size_t>(rows_[y]) * NesEmulator::frame_width;
        for (int x = 0; x < width_; ++x) {
            const std::uint32_t color = row[columns_[x]];
            // Match LVGL's RGB565-to-RGB888 rounding on the original drawing path.
            const auto red   = (((color >> 11U) & 31U) * 2106U) >> 8U;
            const auto green = (((color >> 5U) & 63U) * 1037U) >> 8U;
            const auto blue  = ((color & 31U) * 2106U) >> 8U;
            destination[x]   = 0xff000000U | (red << 16U) | (green << 8U) | blue;
        }
    }
}
