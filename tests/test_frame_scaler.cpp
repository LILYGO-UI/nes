#include "pages/nes/frame_scaler.hpp"
#include "platform/multitouch_reader.hpp"

#include <lvgl.h>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

static void test_multitouch_orientation()
{
    constexpr int panel_width = 568;
    constexpr int panel_height = 1232;
    constexpr int raw_max_x = 1060;
    constexpr int raw_max_y = 2400;
    lv_init();
    auto *display = lv_display_create(panel_width, panel_height);
    assert(display);
    for (const auto rotation : {LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90}) {
        lv_display_set_rotation(display, rotation);
        const int width = lv_display_get_horizontal_resolution(display);
        const int height = lv_display_get_vertical_resolution(display);
        for (const auto &raw : {std::pair{0, 0}, std::pair{raw_max_x, 0},
                                std::pair{0, raw_max_y}, std::pair{raw_max_x, raw_max_y},
                                std::pair{raw_max_x / 2, raw_max_y / 2}, std::pair{317, 1739}}) {
            // Evdev calibrates to the physical panel before LVGL applies rotation.
            lv_point_t expected{raw.first * (panel_width - 1) / raw_max_x,
                                raw.second * (panel_height - 1) / raw_max_y};
            lv_display_rotate_point(display, &expected);
            const auto mapped = MultitouchReader::map_point(raw.first, raw.second, true,
                                                            raw_max_x, raw_max_y, width, height);
            assert(mapped.active);
            assert(mapped.x == expected.x && mapped.y == expected.y);
        }
        const auto released = MultitouchReader::map_point(100, 200, false, raw_max_x, raw_max_y, width, height);
        assert(!released.active && released.x == -1 && released.y == -1);
        assert(!MultitouchReader::map_point(-1, 200, true, raw_max_x, raw_max_y, width, height).active);
        assert(!MultitouchReader::map_point(100, -1, true, raw_max_x, raw_max_y, width, height).active);
    }
    lv_display_delete(display);
    lv_deinit();
}

int main()
{
    test_multitouch_orientation();
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
