#include "app.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

constexpr int kPartialRows                   = 80;
constexpr std::uint32_t kColorPageBackground = 0xf2f2f7;
constexpr std::uint32_t kColorGameBackground = 0x151515;
constexpr char kLongTitle[] =
    "NROM Smoke Test With A Long Game Title That Must Wrap Across Multiple Lines And Remain Fully Visible";

std::vector<lv_color32_t> framebuffer;
int display_width = 0;
lv_point_t pointer_position{};
lv_indev_state_t pointer_state = LV_INDEV_STATE_RELEASED;

void read_pointer(lv_indev_t *, lv_indev_data_t *data)
{
    data->point = pointer_position;
    data->state = pointer_state;
}

void pointer_input(lv_indev_t *pointer, lv_point_t position, lv_indev_state_t state)
{
    pointer_position = position;
    pointer_state    = state;
    lv_tick_inc(20);
    lv_indev_read(pointer);
}

lv_point_t object_center(lv_obj_t *object)
{
    lv_area_t area{};
    lv_obj_get_coords(object, &area);
    return {(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};
}

void tap(lv_indev_t *pointer, lv_obj_t *object)
{
    const auto point = object_center(object);
    pointer_input(pointer, point, LV_INDEV_STATE_PRESSED);
    pointer_input(pointer, point, LV_INDEV_STATE_RELEASED);
}

void swipe(lv_indev_t *pointer, lv_point_t start, int dx, int dy)
{
    pointer_input(pointer, start, LV_INDEV_STATE_PRESSED);
    constexpr int steps = 6;
    for (int step = 1; step <= steps; ++step) {
        pointer_input(pointer, {start.x + dx * step / steps, start.y + dy * step / steps}, LV_INDEV_STATE_PRESSED);
    }
    pointer_input(pointer, {start.x + dx, start.y + dy}, LV_INDEV_STATE_RELEASED);
}

void flush_display(lv_display_t *display, const lv_area_t *area, std::uint8_t *pixels)
{
    const int width    = area->x2 - area->x1 + 1;
    const auto *source = reinterpret_cast<const lv_color32_t *>(pixels);

    for (int y = area->y1; y <= area->y2; ++y) {
        const auto source_offset = static_cast<std::size_t>(y - area->y1) * static_cast<std::size_t>(width);
        const auto destination_offset =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(display_width) + static_cast<std::size_t>(area->x1);
        std::copy_n(source + source_offset, width, framebuffer.begin() + destination_offset);
    }
    lv_display_flush_ready(display);
}

bool write_ppm(const char *path, int width, int height)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto &pixel : framebuffer) {
        file.put(static_cast<char>(pixel.red));
        file.put(static_cast<char>(pixel.green));
        file.put(static_cast<char>(pixel.blue));
    }
    return file.good();
}

void write_test_rom(const std::filesystem::path &path)
{
    std::ofstream file(path, std::ios::binary);
    assert(file);
    const std::array<unsigned char, 16> header{'N', 'E', 'S', 0x1a, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    file.write(reinterpret_cast<const char *>(header.data()), static_cast<std::streamsize>(header.size()));

    std::array<unsigned char, 16384> program{};
    program.fill(0xea);
    // Enable vblank NMI with rendering disabled, leaving the palette backdrop visible.
    const unsigned char reset[] = {
        0x78, 0xd8, 0xa2, 0xff, 0x9a, 0xa9, 0x00,
        0x8d, 0x00, 0x20, 0x8d, 0x01, 0x20, 0x85, 0x00,
        0xa9, 0x80, 0x8d, 0x00, 0x20, 0x4c, 0x14, 0x80,
    };
    // Each NMI changes the backdrop and moves PPUADDR outside palette RAM;
    // otherwise a disabled PPU displays the unchanged entry at $3f01.
    const unsigned char nmi[] = {
        0x48, 0x2c, 0x02, 0x20, 0xa9, 0x3f, 0x8d, 0x06, 0x20,
        0xa9, 0x00, 0x8d, 0x06, 0x20, 0xe6, 0x00, 0xa5, 0x00,
        0x29, 0x0f, 0x09, 0x20, 0x8d, 0x07, 0x20,
        0xa9, 0x20, 0x8d, 0x06, 0x20, 0xa9, 0x00, 0x8d, 0x06, 0x20, 0x68, 0x40,
    };
    std::copy_n(reset, sizeof(reset), program.begin());
    std::copy_n(nmi, sizeof(nmi), program.begin() + 0x100);
    program[0x3ffa] = 0x00;
    program[0x3ffb] = 0x81;
    program[0x3ffc] = 0x00;
    program[0x3ffd] = 0x80;
    program[0x3ffe] = 0x00;
    program[0x3fff] = 0x80;
    file.write(reinterpret_cast<const char *>(program.data()), static_cast<std::streamsize>(program.size()));

    std::array<unsigned char, 8192> characters{};
    file.write(reinterpret_cast<const char *>(characters.data()), static_cast<std::streamsize>(characters.size()));
}

bool colors_differ(const lv_color32_t &left, const lv_color32_t &right)
{
    return left.red != right.red || left.green != right.green || left.blue != right.blue || left.alpha != right.alpha;
}

void run_timers(int milliseconds)
{
    constexpr int step = 5;
    for (int elapsed = 0; elapsed < milliseconds; elapsed += step) {
        std::this_thread::sleep_for(std::chrono::milliseconds(step));
        lv_tick_inc(step);
        lv_timer_handler();
    }
}

void assert_animated_frame(lv_obj_t *frame, lv_obj_t *image)
{
    lv_obj_update_layout(frame);
    const auto sample = object_center(frame);
    const auto offset = static_cast<std::size_t>(sample.y) * static_cast<std::size_t>(display_width) +
                        static_cast<std::size_t>(sample.x);
    auto previous_pixel = framebuffer.at(offset);
    std::uint32_t previous_change = 0;
    int changed_frames = 0;
    int fast_updates = 0;

    // Only LVGL's virtual tick measures refresh cadence; wall time lets the emulator produce frames.
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        lv_tick_inc(16);
        lv_timer_handler();
        const auto pixel = framebuffer.at(offset);
        if (!colors_differ(pixel, previous_pixel)) continue;
        const auto now = lv_tick_get();
        if (changed_frames > 0 && now - previous_change < 33) ++fast_updates;
        previous_pixel = pixel;
        previous_change = now;
        ++changed_frames;
        if (changed_frames >= 4 && fast_updates >= 3) break;
    }

    assert(!lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN));
    assert(changed_frames >= 4);
    assert(fast_updates >= 3);
}

void assert_frame_layout(lv_obj_t *frame, lv_obj_t *image)
{
    const int image_width = lv_image_get_src_width(image) * lv_image_get_scale_x(image) / LV_SCALE_NONE;
    const int image_height = lv_image_get_src_height(image) * lv_image_get_scale_y(image) / LV_SCALE_NONE;
    assert(image_width <= lv_obj_get_width(frame));
    assert(image_height <= lv_obj_get_height(frame));
    assert(image_width >= lv_obj_get_width(frame) - 3);
    assert(image_height >= lv_obj_get_height(frame) - 3);
    assert(lv_image_get_rotation(image) == 0);
    assert(!lv_image_get_antialias(image));

    const auto frame_center = object_center(frame);
    const auto image_center = object_center(image);
    assert(std::abs(frame_center.x - image_center.x) <= 1);
    assert(std::abs(frame_center.y - image_center.y) <= 1);
}

void benchmark_render(lv_display_t *display, lv_obj_t *image, int width, int height)
{
    const char *enabled = std::getenv("NES_RENDER_BENCHMARK");
    if (!enabled || std::string(enabled) != "1") return;

    // Keep the virtual tick and image pixels fixed to isolate LVGL drawing from frame production and scaling.
    constexpr int warmup_frames = 20;
    constexpr int measured_frames = 100;
    for (int frame = 0; frame < warmup_frames; ++frame) {
        lv_obj_invalidate(image);
        lv_refr_now(display);
    }
    const auto started = std::chrono::steady_clock::now();
    for (int frame = 0; frame < measured_frames; ++frame) {
        lv_obj_invalidate(image);
        lv_refr_now(display);
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started);
    std::printf("NES LVGL render-only benchmark: %dx%d, %d frames, %.3f ms/frame "
                "(excludes emulator and frame preparation)\n",
                width, height, measured_frames, elapsed.count() / measured_frames);
}

void assert_inside(lv_obj_t *object, int width, int height)
{
    lv_area_t area{};
    lv_obj_get_coords(object, &area);
    assert(area.x1 >= 0);
    assert(area.y1 >= 0);
    assert(area.x2 < width);
    assert(area.y2 < height);
}

void assert_not_clipped(lv_obj_t *object)
{
    lv_area_t area{};
    lv_obj_get_coords(object, &area);
    for (auto *parent = lv_obj_get_parent(object); parent; parent = lv_obj_get_parent(parent)) {
        lv_area_t parent_area{};
        lv_obj_get_coords(parent, &parent_area);
        assert(area.x1 >= parent_area.x1);
        assert(area.y1 >= parent_area.y1);
        assert(area.x2 <= parent_area.x2);
        assert(area.y2 <= parent_area.y2);
    }
}

}  // namespace

int main(int argc, char **argv)
{
    assert(argc == 4);
    const int width  = std::atoi(argv[1]);
    const int height = std::atoi(argv[2]);
    assert(width >= 320 && height >= 240);

    const auto test_directory =
        std::filesystem::temp_directory_path() /
        ("lilygo-ui-nes-render-" + std::to_string(static_cast<long long>(getpid())) + "-" + std::to_string(width));
    std::filesystem::create_directories(test_directory);
    const auto rom_path = test_directory / (std::string(kLongTitle) + ".nes");
    write_test_rom(rom_path);
    assert(setenv("NES_ROM_DIR", test_directory.c_str(), 1) == 0);

    display_width = width;
    framebuffer.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    std::vector<lv_color32_t> draw_buffer(static_cast<std::size_t>(std::max(width, height)) * kPartialRows);

    lv_init();
    auto *display = lv_display_create(width, height);
    assert(display);
    lv_display_set_buffers(display, draw_buffer.data(), nullptr,
                           static_cast<std::uint32_t>(width * kPartialRows * sizeof(lv_color32_t)),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_display);
    lv_display_set_default(display);
    auto *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer, display);
    lv_indev_set_read_cb(pointer, read_pointer);
    lv_indev_set_mode(pointer, LV_INDEV_MODE_EVENT);

    cm0_app_context_t context{};
    context.root           = lv_screen_active();
    const auto *descriptor = cm0_app_get_descriptor();
    descriptor->open(&context);
    lv_obj_update_layout(context.root);

    auto *surface = lv_obj_find_by_name(context.root, "app_surface");
    auto *header  = lv_obj_find_by_name(context.root, "app_header");
    auto *title   = lv_obj_find_by_name(context.root, "app_title");
    auto *library = lv_obj_find_by_name(context.root, "nes_library");
    auto *list    = lv_obj_find_by_name(context.root, "nes_rom_list");
    auto *row     = lv_obj_find_by_name(context.root, "nes_rom_row");
    auto *refresh = lv_obj_find_by_name(context.root, "nes_refresh");
    auto *status  = lv_obj_find_by_name(context.root, "nes_status");
    auto *rom_title = lv_obj_find_by_name(context.root, "nes_rom_title");
    assert(surface && header && title && library && list && row && refresh && status);
    assert(rom_title);
    assert(lv_color_eq(lv_obj_get_style_bg_color(surface, LV_PART_MAIN), lv_color_hex(kColorPageBackground)));
    assert(lv_obj_get_style_bg_grad_dir(surface, LV_PART_MAIN) == LV_GRAD_DIR_NONE);
    assert(std::string(lv_label_get_text(title)) == "NES");
    assert(std::string(lv_label_get_text(status)) == "1 ROM ready");
    assert_inside(refresh, width, height);
    assert_inside(row, width, height);
    assert_not_clipped(rom_title);
    assert(std::string(lv_label_get_text(rom_title)) == kLongTitle);
    lv_point_t text_size{};
    lv_text_get_size(&text_size, kLongTitle, lv_obj_get_style_text_font(rom_title, LV_PART_MAIN),
                    lv_obj_get_style_text_letter_space(rom_title, LV_PART_MAIN),
                    lv_obj_get_style_text_line_space(rom_title, LV_PART_MAIN),
                    lv_obj_get_content_width(rom_title), LV_TEXT_FLAG_NONE);
    assert(lv_obj_get_content_height(rom_title) >= text_size.y);
    assert(text_size.y > lv_font_get_line_height(lv_obj_get_style_text_font(rom_title, LV_PART_MAIN)));

#if LV_USE_SYSMON
    // Let the native monitor timers populate the screenshot with current data.
    run_timers(600);
#endif
    lv_refr_now(display);
    const std::filesystem::path screenshot_path(argv[3]);
    const auto library_screenshot = screenshot_path.parent_path() / (screenshot_path.stem().string() + "-library" +
                                                                     screenshot_path.extension().string());
    assert(write_ppm(library_screenshot.c_str(), width, height));

    tap(pointer, rom_title);
    lv_obj_update_layout(context.root);
    auto *game      = lv_obj_find_by_name(context.root, "nes_game");
    auto *frame     = lv_obj_find_by_name(context.root, "nes_frame");
    auto *image     = lv_obj_find_by_name(context.root, "nes_frame_image");
    auto *button_a  = lv_obj_find_by_name(context.root, "nes_control_a");
    auto *button_b  = lv_obj_find_by_name(context.root, "nes_control_b");
    auto *button_up = lv_obj_find_by_name(context.root, "nes_control_up");
    auto *stop      = lv_obj_find_by_name(context.root, "nes_stop");
    assert(game && frame && image && button_a && button_b && button_up && stop);
    assert(!lv_obj_has_flag(game, LV_OBJ_FLAG_HIDDEN));
    assert(lv_color_eq(lv_obj_get_style_bg_color(game, LV_PART_MAIN), lv_color_hex(kColorGameBackground)));
    assert_inside(frame, width, height);
    assert_inside(button_a, width, height);
    assert_inside(button_b, width, height);
    assert_inside(button_up, width, height);
    assert_inside(stop, width, height);

    for (const auto *name : {"nes_control_up", "nes_control_down", "nes_control_left", "nes_control_right",
                             "nes_control_a", "nes_control_b"}) {
        auto *button = lv_obj_find_by_name(context.root, name);
        assert(button);
        assert_not_clipped(button);
        assert(lv_obj_get_width(button) == lv_obj_get_height(button));
    }
    lv_area_t area_a{}, area_b{};
    lv_obj_get_coords(button_a, &area_a);
    lv_obj_get_coords(button_b, &area_b);
    assert(area_a.x1 - area_b.x2 - 1 >= 32);

    assert_animated_frame(frame, image);
    assert_frame_layout(frame, image);
#if LV_USE_SYSMON
    run_timers(600);
#endif
    benchmark_render(display, image, width, height);
    const bool has_contrast = std::any_of(framebuffer.cbegin() + 1, framebuffer.cend(),
                                          [](const auto &pixel) { return colors_differ(pixel, framebuffer[0]); });
    assert(has_contrast);
    assert(write_ppm(argv[3], width, height));

    lv_obj_send_event(button_up, LV_EVENT_PRESSED, nullptr);
    lv_obj_send_event(button_a, LV_EVENT_PRESSED, nullptr);
    run_timers(20);
    lv_obj_send_event(button_up, LV_EVENT_RELEASED, nullptr);
    lv_obj_send_event(button_a, LV_EVENT_RELEASED, nullptr);

    const auto resize_running_game = [&](int new_width, int new_height) {
        display_width = new_width;
        lv_display_set_resolution(display, new_width, new_height);
        lv_display_set_buffers(display, draw_buffer.data(), nullptr,
                               static_cast<std::uint32_t>(new_width * kPartialRows * sizeof(lv_color32_t)),
                               LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_obj_update_layout(context.root);
        assert(!lv_obj_has_flag(game, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
        assert_frame_layout(frame, image);
        assert_inside(frame, new_width, new_height);
        assert_inside(button_a, new_width, new_height);
        assert_inside(button_up, new_width, new_height);
        assert_inside(stop, new_width, new_height);
        assert_animated_frame(frame, image);
    };
    resize_running_game(height, width);
    resize_running_game(width, height);

    lv_obj_send_event(stop, LV_EVENT_CLICKED, nullptr);
    run_timers(160);
    assert(!lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(game, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN));

    for (const auto *name : {"nes_rom_icon", "nes_rom_metadata"}) {
        auto *target = lv_obj_find_by_name(context.root, name);
        assert(target);
        tap(pointer, target);
        assert(!lv_obj_has_flag(game, LV_OBJ_FLAG_HIDDEN));
        assert_animated_frame(frame, image);
        lv_obj_send_event(stop, LV_EVENT_CLICKED, nullptr);
        run_timers(160);
        assert(!lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(game, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN));
    }

    auto *delete_button = lv_obj_find_by_name(context.root, "nes_rom_delete");
    assert(delete_button && lv_obj_has_flag(delete_button, LV_OBJ_FLAG_HIDDEN));
    auto swipe_start = object_center(row);
    swipe(pointer, swipe_start, -120, 0);
    run_timers(120);
    lv_obj_update_layout(context.root);
    assert(!lv_obj_has_flag(delete_button, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    assert(std::filesystem::exists(rom_path));
    assert_not_clipped(delete_button);
    lv_refr_now(display);
    const auto delete_screenshot = screenshot_path.parent_path() / (screenshot_path.stem().string() + "-delete" +
                                                                    screenshot_path.extension().string());
    assert(write_ppm(delete_screenshot.c_str(), width, height));

    swipe(pointer, object_center(row), 120, 0);
    lv_obj_update_layout(context.root);
    assert(lv_obj_has_flag(delete_button, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));

    // Fill the list to verify vertical scrolling does not trigger launch or deletion.
    for (int index = 0; index < 12; ++index) {
        write_test_rom(test_directory / ("Z Test Game " + std::to_string(index) + ".nes"));
    }
    tap(pointer, refresh);
    lv_obj_update_layout(context.root);
    row = lv_obj_find_by_name(context.root, "nes_rom_row");
    delete_button = lv_obj_find_by_name(context.root, "nes_rom_delete");
    assert(row && delete_button);
    swipe(pointer, object_center(row), 0, -120);
    run_timers(240);
    assert(lv_obj_get_scroll_y(list) > 0);
    assert(lv_obj_has_flag(delete_button, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    lv_obj_update_layout(context.root);

    swipe(pointer, object_center(row), -120, 0);
    lv_obj_update_layout(context.root);
    assert(!lv_obj_has_flag(delete_button, LV_OBJ_FLAG_HIDDEN));

    swipe(pointer, object_center(delete_button), 0, -120);
    run_timers(240);
    assert(std::filesystem::exists(rom_path));
    assert(lv_obj_get_scroll_y(list) > 0);
    assert(!lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    lv_obj_update_layout(context.root);
    assert(!lv_obj_has_flag(delete_button, LV_OBJ_FLAG_HIDDEN));

    const auto saved_rom_path = test_directory / "saved-rom.bin";
    std::filesystem::rename(rom_path, saved_rom_path);
    std::filesystem::create_directory(rom_path);
    tap(pointer, delete_button);
    run_timers(40);
    assert(lv_obj_find_by_name(context.root, "nes_rom_row") == row);
    auto *rom_count = lv_obj_find_by_name(context.root, "nes_rom_count");
    assert(rom_count && std::string(lv_label_get_text(rom_count)) == "ROMs: 13");
    assert(std::string(lv_label_get_text(status)).find("Could not delete ROM:") == 0);
    assert_not_clipped(status);
    assert(std::filesystem::is_directory(rom_path));
    std::filesystem::remove(rom_path);
    std::filesystem::rename(saved_rom_path, rom_path);

    tap(pointer, delete_button);
    run_timers(40);
    lv_obj_update_layout(context.root);
    assert(!std::filesystem::exists(rom_path));
    assert(!lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    assert(rom_count && std::string(lv_label_get_text(rom_count)) == "ROMs: 12");
    descriptor->close();
    lv_indev_delete(pointer);
    lv_display_delete(display);
    lilygo_ui_fonts_deinit();
    lv_deinit();
    std::filesystem::remove_all(test_directory);
    return 0;
}
