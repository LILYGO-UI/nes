#include "pages/nes/frame_scaler.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

int main()
{
    std::array<std::uint16_t, NesEmulator::frame_pixels> source{};
    NesFrameScaler scaler;
    scaler.update(source);
    assert(scaler.pixels().empty());
    assert(!scaler.resize(0, 240));

    for (int y = 0; y < NesEmulator::frame_height; ++y) {
        for (int x = 0; x < NesEmulator::frame_width; ++x) {
            source[y * NesEmulator::frame_width + x] =
                y < 120 ? (x < 128 ? 0xf800 : 0x07e0) : (x < 128 ? 0x001f : 0xffff);
        }
    }

    for (const auto &size : {std::pair{512, 480}, std::pair{413, 387}, std::pair{128, 120}}) {
        assert(scaler.resize(size.first, size.second));
        scaler.update(source);
        const auto &pixels = scaler.pixels();
        assert(pixels.size() == static_cast<std::size_t>(size.first * size.second));
        assert(pixels.front() == 0xffff0000U);
        assert(pixels[size.first - 1] == 0xff00ff00U);
        assert(pixels[(size.second - 1) * size.first] == 0xff0000ffU);
        assert(pixels.back() == 0xffffffffU);
        // Check every sample across noninteger scales, including repeated rows.
        for (int y = 0; y < size.second; ++y) {
            for (int x = 0; x < size.first; ++x) {
                const bool top      = y * 240 / size.second < 120;
                const bool left     = x * 256 / size.first < 128;
                const auto expected = top ? (left ? 0xffff0000U : 0xff00ff00U) : (left ? 0xff0000ffU : 0xffffffffU);
                assert(pixels[y * size.first + x] == expected);
            }
        }
        const auto *storage = pixels.data();
        assert(!scaler.resize(size.first, size.second));
        assert(scaler.pixels().data() == storage);
    }

    source.fill(0);
    scaler.update(source);
    for (const auto pixel : scaler.pixels()) assert(pixel == 0xff000000U);

    if (std::getenv("NES_RENDER_BENCHMARK")) {
        for (const auto &size : {std::pair{528, 495}, std::pair{401, 375}}) {
            scaler.resize(size.first, size.second);
            constexpr int frames = 200;
            const auto start     = std::chrono::steady_clock::now();
            for (int frame = 0; frame < frames; ++frame) scaler.update(source);
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
            std::printf("NES pre-scale %dx%d: %.3f ms/frame\n", size.first, size.second, elapsed.count() / frames);
        }
    }
}
