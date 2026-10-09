#include "app.hpp"
#include "domain/nes_paths.hpp"
#include "pages/nes/nes_view.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

constexpr int kPartialRows                   = 80;
constexpr std::uint32_t kColorPageBackground = 0xf2f2f7;
constexpr std::uint32_t kColorGameBackground = 0x151515;
constexpr char kLongTitle[] =
    "NROM Smoke Test With A Long Game Title That Must Wrap Across Multiple Lines And Remain Fully Visible "
    "In Both Portrait And Landscape With The Expanded Full Width Library List And Its Bottom Toolbar";
static_assert(sizeof(kLongTitle) + sizeof(".nes") - 2 < 255, "The test ROM filename must fit filesystem limits");

std::vector<lv_color32_t> framebuffer;
int display_width = 0;
lv_point_t pointer_position{};
lv_indev_state_t pointer_state = LV_INDEV_STATE_RELEASED;
std::uint32_t keyboard_key = 0;
lv_indev_state_t keyboard_state = LV_INDEV_STATE_RELEASED;

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

void read_keyboard(lv_indev_t *, lv_indev_data_t *data)
{
    data->key = keyboard_key;
    data->state = keyboard_state;
}

void keyboard_input(lv_indev_t *keyboard, std::uint32_t key, lv_indev_state_t state)
{
    keyboard_key = key;
    keyboard_state = state;
    lv_tick_inc(20);
    lv_indev_read(keyboard);
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

void assert_game_controls(lv_obj_t *root, lv_obj_t *frame, int width, int height)
{
    static constexpr const char *names[] = {"nes_control_up", "nes_control_down", "nes_control_left",
                                           "nes_control_right", "nes_control_a", "nes_control_b",
                                           "nes_control_select", "nes_control_start", "nes_stop"};
    std::array<lv_area_t, 13> areas{};
    assert_inside(frame, width, height);
    assert_not_clipped(frame);
    lv_obj_get_coords(frame, &areas[9]);
    auto *toolbar = lv_obj_find_by_name(root, "nes_state_toolbar");
    assert(toolbar);
    assert_inside(toolbar, width, height);
    assert_not_clipped(toolbar);
    lv_obj_get_coords(toolbar, &areas[10]);
    auto *system_controls = lv_obj_find_by_name(root, "nes_system_controls");
    assert(system_controls);
    assert_inside(system_controls, width, height);
    assert_not_clipped(system_controls);
    auto *status = lv_obj_find_by_name(root, "nes_state_status");
    assert(status && lv_obj_get_parent(status) == toolbar);
    assert_inside(status, width, height);
    assert_not_clipped(status);
    assert(lv_obj_get_width(status) > 0);
    for (std::size_t index = 0; index < 2; ++index) {
        const auto *name = index == 0 ? "nes_save_state" : "nes_load_state";
        auto *control = lv_obj_find_by_name(root, name);
        assert(control && lv_obj_get_parent(control) == system_controls);
        assert_inside(control, width, height);
        assert_not_clipped(control);
        assert(lv_obj_get_width(control) > 0);
        assert(lv_obj_get_height(control) >= 44);
        assert_not_clipped(lv_obj_get_child(control, 0));
        lv_obj_get_coords(control, &areas[11 + index]);
    }
    for (std::size_t index = 0; index < 9; ++index) {
        auto *button = lv_obj_find_by_name(root, names[index]);
        assert(button);
        assert_inside(button, width, height);
        assert_not_clipped(button);
        assert_not_clipped(lv_obj_get_child(button, 0));
        if (index < 6) {
            assert(lv_obj_get_width(button) == lv_obj_get_height(button));
            assert(lv_obj_get_width(button) >= 100);
        } else {
            assert(lv_obj_get_height(button) >= 44);
        }
        lv_obj_get_coords(button, &areas[index]);
    }
    auto *select = lv_obj_find_by_name(root, "nes_control_select");
    auto *start = lv_obj_find_by_name(root, "nes_control_start");
    auto *stop = lv_obj_find_by_name(root, "nes_stop");
    assert(select && start && stop);
    assert(lv_obj_get_parent(select) == system_controls);
    assert(lv_obj_get_parent(start) == system_controls);
    assert(lv_obj_get_parent(stop) == system_controls);
    // Save, Load, Select, Start, and Stop are one horizontal control row.
    for (std::size_t index = 6; index < 9; ++index) {
        for (std::size_t state_index = 11; state_index < 13; ++state_index) {
            assert(areas[index].y1 == areas[state_index].y1);
            assert(areas[index].y2 == areas[state_index].y2);
        }
    }
    for (std::size_t first = 0; first < areas.size(); ++first) {
        for (std::size_t second = first + 1; second < areas.size(); ++second) {
            const auto &a = areas[first];
            const auto &b = areas[second];
            assert(a.x2 < b.x1 || b.x2 < a.x1 || a.y2 < b.y1 || b.y2 < a.y1);
        }
    }
    // A is upper-right of B and their equal horizontal/vertical center deltas
    // form a 45-degree diagonal with no shared touch area.
    assert(areas[4].x1 > areas[5].x1);
    assert(areas[4].y1 < areas[5].y1);
    const auto a_center_x = (areas[4].x1 + areas[4].x2) / 2;
    const auto a_center_y = (areas[4].y1 + areas[4].y2) / 2;
    const auto b_center_x = (areas[5].x1 + areas[5].x2) / 2;
    const auto b_center_y = (areas[5].y1 + areas[5].y2) / 2;
    assert(a_center_x - b_center_x == b_center_y - a_center_y);
}

void assert_library_layout(lv_obj_t *root, int width, int height)
{
    auto *list = lv_obj_find_by_name(root, "nes_rom_list");
    auto *toolbar = lv_obj_find_by_name(root, "nes_library_toolbar");
    assert(list && toolbar);
    assert(!lv_obj_find_by_name(root, "nes_library_summary"));
    assert(!lv_obj_find_by_name(root, "nes_library_paths"));
    assert(!lv_obj_find_by_name(root, "nes_rom_count"));
    assert(!lv_obj_find_by_name(root, "nes_status"));
    assert_inside(list, width, height);
    assert_inside(toolbar, width, height);
    assert_not_clipped(list);
    assert_not_clipped(toolbar);
    lv_area_t list_area{}, toolbar_area{};
    lv_obj_get_coords(list, &list_area);
    lv_obj_get_coords(toolbar, &toolbar_area);
    assert(list_area.y2 < toolbar_area.y1);
    assert(lv_obj_get_height(list) > height / 2);
    for (const auto *name : {"nes_refresh", "nes_help", "nes_settings"}) {
        auto *button = lv_obj_find_by_name(root, name);
        assert(button && lv_obj_get_parent(button) == toolbar);
        assert_inside(button, width, height);
        assert_not_clipped(button);
        assert_not_clipped(lv_obj_get_child(button, 0));
    }
    auto *add = lv_obj_find_by_name(root, "nes_add_rom");
    assert(add && lv_obj_get_parent(add) == list);
    assert(lv_obj_get_child(list, -1) == add);
    assert(lv_obj_get_width(add) == lv_obj_get_content_width(list));
    assert(lv_obj_get_height(add) >= 44);
}

lv_obj_t *assert_error_dialog(lv_obj_t *root, const std::string &message, int width, int height)
{
    auto *overlay = lv_obj_find_by_name(root, "nes_error_overlay");
    auto *dialog = lv_obj_find_by_name(root, "nes_error_dialog");
    auto *title = lv_obj_find_by_name(root, "nes_error_title");
    auto *text = lv_obj_find_by_name(root, "nes_error_message");
    assert(overlay && dialog && title && text);
    assert(!lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN));
    assert(std::string(lv_label_get_text(title)) == LV_SYMBOL_WARNING " Error");
    assert(std::string(lv_label_get_text(text)).find(message) == 0);
    assert(lv_label_get_long_mode(text) == LV_LABEL_LONG_WRAP);
    lv_obj_update_layout(root);
    for (auto *object : {dialog, title, text}) {
        assert_inside(object, width, height);
        assert_not_clipped(object);
    }
    return overlay;
}

void assert_error_timeout(lv_obj_t *overlay, std::uint32_t shown_at)
{
    const auto elapsed = lv_tick_elaps(shown_at);
    assert(elapsed < 4999);
    lv_tick_inc(4999 - elapsed);
    lv_timer_handler();
    assert(!lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN));
    lv_tick_inc(1);
    lv_timer_handler();
    assert(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN));
}

lv_obj_t *scroll_to_add_rom(lv_obj_t *root, int width, int height)
{
    auto *list = lv_obj_find_by_name(root, "nes_rom_list");
    auto *add = lv_obj_find_by_name(root, "nes_add_rom");
    assert(list && add && lv_obj_get_child(list, -1) == add);
    const auto previous_scroll = lv_obj_get_scroll_y(list);
    lv_area_t before{}, after{};
    lv_obj_get_coords(add, &before);
    lv_obj_scroll_to_view(add, LV_ANIM_OFF);
    lv_obj_update_layout(root);
    lv_obj_get_coords(add, &after);
    assert(before.y1 - after.y1 == lv_obj_get_scroll_y(list) - previous_scroll);
    assert_inside(add, width, height);
    assert_not_clipped(add);
    assert_not_clipped(lv_obj_get_child(add, 0));
    return add;
}

void assert_library_help(lv_obj_t *root, lv_indev_t *pointer, lv_display_t *display,
                         const std::filesystem::path &directory, const std::filesystem::path &screenshot_path,
                         int width, int height)
{
    auto *library = lv_obj_find_by_name(root, "nes_library");
    auto *list = lv_obj_find_by_name(root, "nes_rom_list");
    auto *help = lv_obj_find_by_name(root, "nes_help");
    auto *panel = lv_obj_find_by_name(root, "nes_help_panel");
    auto *back = lv_obj_find_by_name(root, "nes_help_back");
    auto *content = lv_obj_find_by_name(root, "nes_help_content");
    auto *path = lv_obj_find_by_name(root, "nes_help_path");
    auto *states_path = lv_obj_find_by_name(root, "nes_help_states_path");
    assert(library && list && help && panel && back && content && path && states_path);
    assert(lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN));
    // Compare a settled position: a swipe can still be rebounding past the list edge.
    for (int attempt = 0; attempt < 100 && lv_obj_is_scrolling(list); ++attempt) run_timers(20);
    assert(!lv_obj_is_scrolling(list));
    const auto library_scroll = lv_obj_get_scroll_y(list);

    tap(pointer, help);
    lv_obj_update_layout(root);
    assert(!lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    assert_inside(back, width, height);
    assert_inside(content, width, height);
    assert_not_clipped(back);
    assert_not_clipped(lv_obj_get_child(back, 0));
    assert(lv_obj_has_flag(content, LV_OBJ_FLAG_SCROLLABLE));
    assert(std::string(lv_label_get_text(path)) == directory.string());
    assert(lv_label_get_long_mode(path) == LV_LABEL_LONG_WRAP);
    assert(std::string(lv_label_get_text(states_path)) == (directory.parent_path() / "states").string());
    assert(lv_label_get_long_mode(states_path) == LV_LABEL_LONG_WRAP);
    lv_point_t text_size{};
    lv_text_get_size(&text_size, directory.c_str(), lv_obj_get_style_text_font(path, LV_PART_MAIN),
                    lv_obj_get_style_text_letter_space(path, LV_PART_MAIN),
                    lv_obj_get_style_text_line_space(path, LV_PART_MAIN),
                    lv_obj_get_content_width(path), LV_TEXT_FLAG_NONE);
    assert(lv_obj_get_content_height(path) >= text_size.y);
    assert(text_size.y > lv_font_get_line_height(lv_obj_get_style_text_font(path, LV_PART_MAIN)));
    lv_obj_scroll_to_view_recursive(path, LV_ANIM_OFF);
    lv_obj_update_layout(root);
    assert_not_clipped(path);

    lv_obj_scroll_to_y(content, 0, LV_ANIM_OFF);
    lv_obj_update_layout(root);
    lv_refr_now(display);
    const auto help_screenshot = screenshot_path.parent_path() /
                                 (screenshot_path.stem().string() + "-help" + screenshot_path.extension().string());
    assert(write_ppm(help_screenshot.c_str(), width, height));

    // Every help section remains reachable in the short landscape viewport.
    auto *last_section = lv_obj_get_child(content, -1);
    assert(last_section);
    lv_obj_scroll_to_view(last_section, LV_ANIM_OFF);
    lv_obj_update_layout(root);
    lv_area_t section_area{}, content_area{};
    lv_obj_get_coords(last_section, &section_area);
    lv_obj_get_coords(content, &content_area);
    assert(section_area.y2 <= content_area.y2);
    assert_not_clipped(states_path);
    assert_inside(back, width, height);
    tap(pointer, back);
    lv_obj_update_layout(root);
    assert(lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_scroll_y(list) == library_scroll);
    assert_library_layout(root, width, height);
}

void assert_game_uses_page(lv_obj_t *surface, lv_obj_t *game, lv_obj_t *frame)
{
    lv_area_t surface_area{}, game_area{};
    lv_obj_get_coords(surface, &surface_area);
    lv_obj_get_coords(game, &game_area);
    assert(surface_area.x1 == game_area.x1 && surface_area.y1 == game_area.y1);
    assert(surface_area.x2 == game_area.x2 && surface_area.y2 == game_area.y2);
    if (lv_obj_get_width(surface) > lv_obj_get_height(surface)) {
        // The old application header limited the standard landscape frame to 390 pixels.
        assert(lv_obj_get_height(frame) > 390);
    }
}

lv_obj_t *find_browser_entry(lv_obj_t *list, const std::string &name)
{
    for (std::uint32_t index = 0; index < lv_obj_get_child_count(list); ++index) {
        auto *row = lv_obj_get_child(list, static_cast<int32_t>(index));
        auto *label = lv_obj_get_child(row, 0);
        if (label && std::string(lv_label_get_text(label)) == name) return row;
    }
    return nullptr;
}

std::vector<char> read_file(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    assert(file);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void assert_state_action(lv_obj_t *root, lv_indev_t *pointer, bool save, const char *expected, bool success)
{
    auto *save_button = lv_obj_find_by_name(root, "nes_save_state");
    auto *load_button = lv_obj_find_by_name(root, "nes_load_state");
    auto *status = lv_obj_find_by_name(root, "nes_state_status");
    auto *game = lv_obj_find_by_name(root, "nes_game");
    auto *library = lv_obj_find_by_name(root, "nes_library");
    auto *error = lv_obj_find_by_name(root, "nes_error_overlay");
    assert(save_button && load_button && status && game && library && error);
    assert(!lv_obj_has_state(save_button, LV_STATE_DISABLED));
    assert(!lv_obj_has_state(load_button, LV_STATE_DISABLED));
    tap(pointer, save ? save_button : load_button);
    assert(std::string(lv_label_get_text(status)) == (save ? "Saving..." : "Loading..."));
    assert(lv_obj_has_state(save_button, LV_STATE_DISABLED));
    assert(lv_obj_has_state(load_button, LV_STATE_DISABLED));

    // Repeated physical taps cannot queue a second operation while the result is pending.
    tap(pointer, save ? load_button : save_button);
    assert(std::string(lv_label_get_text(status)) == (save ? "Saving..." : "Loading..."));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (lv_obj_has_state(save_button, LV_STATE_DISABLED) && std::chrono::steady_clock::now() < deadline) {
        run_timers(10);
    }
    assert(!lv_obj_has_state(save_button, LV_STATE_DISABLED));
    assert(!lv_obj_has_state(load_button, LV_STATE_DISABLED));
    assert(std::string(lv_label_get_text(status)) == expected);
    assert(lv_color_eq(lv_obj_get_style_text_color(status, LV_PART_MAIN),
                       lv_color_hex(success ? 0x34c759 : 0xff3b30)));
    assert(!lv_obj_has_flag(game, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(error, LV_OBJ_FLAG_HIDDEN));
}

void assert_rom_import(lv_obj_t *root, lv_indev_t *pointer, lv_display_t *display,
                       const std::filesystem::path &home, const std::filesystem::path &screenshot_path,
                       int width, int height)
{
    const auto sources = home / "Import Sources";
    const auto empty_directory = sources / "Empty folder";
    const auto source = sources / "Added From File Browser.nes";
    const auto destination = home / "nes" / "rom" / source.filename();
    std::filesystem::create_directories(empty_directory);
    write_test_rom(source);
    write_test_rom(sources / "Uppercase Extension.NES");
    std::ofstream(sources / "Not A ROM.txt") << "Filtered file";

    auto *library = lv_obj_find_by_name(root, "nes_library");
    auto *browser = lv_obj_find_by_name(root, "nes_rom_browser");
    auto *home_button = lv_obj_find_by_name(root, "nes_browser_home");
    auto *root_button = lv_obj_find_by_name(root, "nes_browser_root");
    auto *up = lv_obj_find_by_name(root, "nes_browser_up");
    auto *cancel = lv_obj_find_by_name(root, "nes_browser_cancel");
    auto *path = lv_obj_find_by_name(root, "nes_browser_path");
    auto *status = lv_obj_find_by_name(root, "nes_browser_status");
    auto *list = lv_obj_find_by_name(root, "nes_browser_list");
    assert(library && browser && home_button && root_button && up && cancel && path && status && list);
    assert(lv_obj_has_flag(browser, LV_OBJ_FLAG_HIDDEN));

    const auto click = [&](lv_obj_t *object) {
        assert(object);
        lv_obj_scroll_to_view(object, LV_ANIM_OFF);
        lv_obj_update_layout(root);
        tap(pointer, object);
        lv_obj_update_layout(root);
    };
    const auto click_add = [&]() {
        // Import rebuilds the ROM list, including its trailing Add button.
        click(scroll_to_add_rom(root, width, height));
    };
    const auto expect_path = [&](const std::filesystem::path &expected) {
        if (std::string(lv_label_get_text(path)) != expected.string()) {
            std::fprintf(stderr, "Browser path: expected %s, received %s (%s)\n", expected.c_str(),
                         lv_label_get_text(path), lv_label_get_text(status));
        }
        assert(std::string(lv_label_get_text(path)) == expected.string());
    };
    click_add();
    assert(!lv_obj_has_flag(browser, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    assert(std::string(lv_label_get_text(status)) == "Select a .nes file to copy it to $HOME/nes/rom");
    expect_path(home);
    click(root_button);
    expect_path("/");
    click(up);
    expect_path("/");
    click(home_button);
    expect_path(home);
    click(find_browser_entry(list, sources.filename().string()));
    expect_path(sources);
    assert(lv_obj_get_child_count(list) == 3);
    assert(!find_browser_entry(list, "Not A ROM.txt"));
    assert(find_browser_entry(list, "Uppercase Extension.NES"));
    auto *directory_row = find_browser_entry(list, empty_directory.filename().string());
    assert(directory_row == lv_obj_get_child(list, 0));
    click(directory_row);
    expect_path(empty_directory);
    click(up);
    expect_path(sources);
    lv_refr_now(display);
    const auto browser_screenshot = screenshot_path.parent_path() /
                                    (screenshot_path.stem().string() + "-browser" + screenshot_path.extension().string());
    assert(write_ppm(browser_screenshot.c_str(), width, height));
    for (auto *object : {home_button, root_button, up, cancel, status}) {
        assert_inside(object, width, height);
        assert_not_clipped(object);
    }
    for (auto *button : {home_button, root_button, up, cancel}) {
        assert_not_clipped(lv_obj_get_child(button, 0));
    }
    auto *path_container = lv_obj_get_parent(path);
    assert_not_clipped(path_container);
    assert(lv_label_get_long_mode(path) == LV_LABEL_LONG_WRAP);
    lv_point_t path_text_size{};
    lv_text_get_size(&path_text_size, sources.c_str(), lv_obj_get_style_text_font(path, LV_PART_MAIN),
                    lv_obj_get_style_text_letter_space(path, LV_PART_MAIN),
                    lv_obj_get_style_text_line_space(path, LV_PART_MAIN),
                    lv_obj_get_content_width(path), LV_TEXT_FLAG_NONE);
    assert(lv_obj_get_content_height(path) >= path_text_size.y);
    lv_area_t path_area{}, path_container_area{};
    lv_obj_get_coords(path, &path_area);
    lv_obj_get_coords(path_container, &path_container_area);
    assert(path_area.x1 >= path_container_area.x1 && path_area.x2 <= path_container_area.x2);
    assert(path_area.y1 >= path_container_area.y1);
    lv_obj_scroll_to_y(path_container, lv_obj_get_scroll_y(path_container) + lv_obj_get_scroll_bottom(path_container),
                       LV_ANIM_OFF);
    lv_obj_update_layout(root);
    lv_obj_get_coords(path, &path_area);
    assert(path_area.y2 <= path_container_area.y2);
    lv_obj_scroll_to_y(path_container, 0, LV_ANIM_OFF);
    lv_obj_update_layout(root);
    click(cancel);
    assert(lv_obj_has_flag(browser, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    assert(!std::filesystem::exists(destination));
    assert_library_layout(root, width, height);
    click_add();
    click(home_button);
    click(find_browser_entry(list, sources.filename().string()));
    click(find_browser_entry(list, source.filename().string()));
    assert(lv_obj_has_flag(browser, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    assert(std::filesystem::is_regular_file(source));
    assert(std::filesystem::is_regular_file(destination));
    const auto original_contents = read_file(source);
    assert(read_file(destination) == original_contents);
    assert_library_layout(root, width, height);

    // A conflicting import must not replace an existing library file.
    std::ofstream(source, std::ios::binary | std::ios::app).put('X');
    click_add();
    click(home_button);
    click(find_browser_entry(list, sources.filename().string()));
    const std::string browsing_status = lv_label_get_text(status);
    click(find_browser_entry(list, source.filename().string()));
    const auto error_shown_at = lv_tick_get();
    assert(!lv_obj_has_flag(browser, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    auto *error = assert_error_dialog(root, "A ROM with this filename already exists", width, height);
    assert(std::string(lv_label_get_text(status)) == browsing_status);
    assert(read_file(destination) == original_contents);
    assert(read_file(source).size() == original_contents.size() + 1);
    click(cancel);
    assert(!lv_obj_has_flag(browser, LV_OBJ_FLAG_HIDDEN));
    assert_error_timeout(error, error_shown_at);
    click(cancel);
    assert_library_layout(root, width, height);
    assert(lv_obj_has_flag(error, LV_OBJ_FLAG_HIDDEN));
}

}  // namespace

int main(int argc, char **argv)
{
    assert(argc == 4);
    const int width  = std::atoi(argv[1]);
    const int height = std::atoi(argv[2]);
    assert(width >= 320 && height >= 240);

    const auto temporary_root =
        std::filesystem::temp_directory_path() /
        ("lilygo-ui-nes-render-" + std::to_string(static_cast<long long>(getpid())) + "-" + std::to_string(width));
    const auto test_home = temporary_root / "home-in-a-directory-whose-full-path-must-wrap" /
                           "nested-location-to-verify-that-the-complete-rom-storage-path-remains-readable-in-help";
    const auto test_directory = test_home / "nes" / "rom";
    std::filesystem::create_directories(test_directory);
    const auto rom_path = test_directory / (std::string(kLongTitle) + ".nes");
    write_test_rom(rom_path);
    assert(setenv("HOME", test_home.c_str(), 1) == 0);

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
    // AppKit creates a default group before opening the app. Button autofocus
    // can therefore force layout while the view is still being constructed.
    auto *input_group = lv_group_create();
    lv_group_set_default(input_group);
    lv_indev_set_group(pointer, input_group);
    auto *keyboard = lv_indev_create();
    lv_indev_set_type(keyboard, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_display(keyboard, display);
    lv_indev_set_read_cb(keyboard, read_keyboard);
    lv_indev_set_mode(keyboard, LV_INDEV_MODE_EVENT);
    lv_indev_set_group(keyboard, input_group);

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
    auto *settings = lv_obj_find_by_name(context.root, "nes_settings");
    auto *error_overlay = lv_obj_find_by_name(context.root, "nes_error_overlay");
    auto *rom_title = lv_obj_find_by_name(context.root, "nes_rom_title");
    assert(surface && !header && !title && library && list && row && refresh && settings && error_overlay);
    assert(rom_title);
    assert(lv_color_eq(lv_obj_get_style_bg_color(surface, LV_PART_MAIN), lv_color_hex(kColorPageBackground)));
    assert(lv_obj_get_style_bg_grad_dir(surface, LV_PART_MAIN) == LV_GRAD_DIR_NONE);
    assert(lv_obj_has_flag(error_overlay, LV_OBJ_FLAG_HIDDEN));
    assert_library_layout(context.root, width, height);
    assert_inside(row, width, height);
    const std::filesystem::path screenshot_path(argv[3]);
    assert_library_help(context.root, pointer, display, test_directory, screenshot_path, width, height);

    // The settings page exposes the requested P1 mapping and clearly marks
    // the unsupported second player without changing the library state.
    tap(pointer, settings);
    lv_obj_update_layout(context.root);
    auto *settings_panel = lv_obj_find_by_name(context.root, "nes_settings_panel");
    auto *settings_title = lv_obj_find_by_name(context.root, "nes_settings_title");
    auto *settings_table = lv_obj_find_by_name(context.root, "nes_keymap_table");
    auto *settings_back = lv_obj_find_by_name(context.root, "nes_settings_back");
    auto *settings_p2 = lv_obj_find_by_name(context.root, "nes_settings_p2_note");
    assert(settings_panel && settings_title && settings_table && settings_back && settings_p2);
    assert(!lv_obj_has_flag(settings_panel, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    assert(std::string(lv_label_get_text(settings_title)) == "Key Mapping");
    assert(std::string(lv_label_get_text(settings_p2)).find("not supported") != std::string::npos);
    lv_refr_now(display);
    const auto settings_screenshot = screenshot_path.parent_path() /
                                     (screenshot_path.stem().string() + "-settings" + screenshot_path.extension().string());
    assert(write_ppm(settings_screenshot.c_str(), width, height));
    // Enlarged mapping rows remain readable and reachable in both orientations.
    for (std::uint32_t index = 1; index < lv_obj_get_child_count(settings_table); ++index) {
        auto *mapping_row = lv_obj_get_child(settings_table, static_cast<int32_t>(index));
        lv_obj_scroll_to_view(mapping_row, LV_ANIM_OFF);
        lv_obj_update_layout(context.root);
        assert_inside(mapping_row, width, height);
        assert_not_clipped(mapping_row);
        for (std::uint32_t label_index = 0; label_index < lv_obj_get_child_count(mapping_row); ++label_index) {
            assert_not_clipped(lv_obj_get_child(mapping_row, static_cast<int32_t>(label_index)));
        }
    }
    auto *last_mapping = lv_obj_find_by_name(context.root, "nes_keymap_select");
    assert(last_mapping && last_mapping == lv_obj_get_child(settings_table, -1));
    assert_not_clipped(last_mapping);
    tap(pointer, settings_back);
    assert(lv_obj_has_flag(settings_panel, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
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
    const auto library_screenshot = screenshot_path.parent_path() / (screenshot_path.stem().string() + "-library" +
                                                                     screenshot_path.extension().string());
    assert(write_ppm(library_screenshot.c_str(), width, height));
    const auto capture_library_end = [&]() {
        scroll_to_add_rom(context.root, width, height);
        lv_refr_now(display);
        const auto end_screenshot = screenshot_path.parent_path() /
                                    (screenshot_path.stem().string() + "-library-end" +
                                     screenshot_path.extension().string());
        assert(write_ppm(end_screenshot.c_str(), width, height));
        lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
        lv_obj_update_layout(context.root);
    };
    capture_library_end();

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
    assert_game_uses_page(surface, game, frame);
    assert(lv_color_eq(lv_obj_get_style_bg_color(game, LV_PART_MAIN), lv_color_hex(kColorGameBackground)));
    assert_game_controls(context.root, frame, width, height);

    assert_animated_frame(frame, image);
    assert_frame_layout(frame, image);
    const auto state_path = std::filesystem::path(nes_paths::state_path(rom_path.string()));
    assert(state_path.parent_path() == test_home / "nes" / "states");
    assert(!std::filesystem::exists(state_path));
    assert_state_action(context.root, pointer, false, "No saved state", false);
    assert_animated_frame(frame, image);
    lv_refr_now(display);
    const auto missing_state_screenshot = screenshot_path.parent_path() /
                                         (screenshot_path.stem().string() + "-state-missing" +
                                          screenshot_path.extension().string());
    assert(write_ppm(missing_state_screenshot.c_str(), width, height));
    assert_state_action(context.root, pointer, true, "State saved", true);
    assert(std::filesystem::is_regular_file(state_path));
    assert(std::filesystem::file_size(state_path) > 0);
    assert(!std::filesystem::exists(test_directory / state_path.filename()));
    assert_state_action(context.root, pointer, false, "State loaded", true);
    assert_animated_frame(frame, image);
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
        assert_game_uses_page(surface, game, frame);
        assert_frame_layout(frame, image);
        assert_game_controls(context.root, frame, new_width, new_height);
        assert_state_action(context.root, pointer, false, "State loaded", true);
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
        assert_state_action(context.root, pointer, false, "State loaded", true);
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
    assert(lv_obj_has_flag(error_overlay, LV_OBJ_FLAG_HIDDEN));
    assert_library_layout(context.root, width, height);
    scroll_to_add_rom(context.root, width, height);
    assert(lv_obj_get_scroll_y(list) > 0);
    capture_library_end();
    row = lv_obj_find_by_name(context.root, "nes_rom_row");
    delete_button = lv_obj_find_by_name(context.root, "nes_rom_delete");
    assert(row && delete_button);
    swipe(pointer, object_center(row), 0, -120);
    run_timers(240);
    assert(lv_obj_get_scroll_y(list) > 0);
    assert(lv_obj_has_flag(delete_button, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    assert_library_help(context.root, pointer, display, test_directory, screenshot_path, width, height);
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
    const auto delete_error_shown_at = lv_tick_get();
    assert(lv_obj_find_by_name(context.root, "nes_rom_row") == row);
    assert_library_layout(context.root, width, height);
    assert_error_dialog(context.root, "Could not delete ROM:", width, height);
    lv_refr_now(display);
    const auto error_screenshot = screenshot_path.parent_path() /
                                 (screenshot_path.stem().string() + "-error" + screenshot_path.extension().string());
    assert(write_ppm(error_screenshot.c_str(), width, height));
    // The alert blocks toolbar actions, then closes exactly five seconds after the failure.
    tap(pointer, settings);
    assert(lv_obj_has_flag(settings_panel, LV_OBJ_FLAG_HIDDEN));
    lv_group_focus_obj(settings);
    assert(lv_group_get_focused(input_group) == settings);
    assert(lv_indev_get_group(keyboard) == nullptr);
    for (const auto key : {LV_KEY_ENTER, LV_KEY_NEXT, LV_KEY_PREV}) {
        keyboard_input(keyboard, key, LV_INDEV_STATE_PRESSED);
        keyboard_input(keyboard, key, LV_INDEV_STATE_RELEASED);
        assert(lv_obj_has_flag(settings_panel, LV_OBJ_FLAG_HIDDEN));
        assert(lv_group_get_focused(input_group) == settings);
    }
    // A key held when the alert expires cannot activate the page on release.
    keyboard_input(keyboard, LV_KEY_ENTER, LV_INDEV_STATE_PRESSED);
    assert_error_timeout(error_overlay, delete_error_shown_at);
    assert(lv_indev_get_group(keyboard) == input_group);
    keyboard_input(keyboard, LV_KEY_ENTER, LV_INDEV_STATE_RELEASED);
    assert(lv_obj_has_flag(settings_panel, LV_OBJ_FLAG_HIDDEN));
    assert(lv_group_get_focused(input_group) == settings);
    keyboard_input(keyboard, LV_KEY_ENTER, LV_INDEV_STATE_PRESSED);
    keyboard_input(keyboard, LV_KEY_ENTER, LV_INDEV_STATE_RELEASED);
    assert(!lv_obj_has_flag(settings_panel, LV_OBJ_FLAG_HIDDEN));
    tap(pointer, settings_back);
    assert(lv_obj_has_flag(error_overlay, LV_OBJ_FLAG_HIDDEN));
    // Retrying the same failed action opens a new alert with a fresh timeout.
    tap(pointer, delete_button);
    const auto repeated_error_shown_at = lv_tick_get();
    assert_error_dialog(context.root, "Could not delete ROM:", width, height);
    assert_error_timeout(error_overlay, repeated_error_shown_at);
    assert(std::filesystem::is_directory(rom_path));
    std::filesystem::remove(rom_path);
    std::filesystem::rename(saved_rom_path, rom_path);

    tap(pointer, delete_button);
    run_timers(40);
    lv_obj_update_layout(context.root);
    assert(!std::filesystem::exists(rom_path));
    assert(!lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    assert_library_layout(context.root, width, height);
    assert_rom_import(context.root, pointer, display, test_home, screenshot_path, width, height);

    // The empty state keeps Add in the list and Help available below it.
    std::filesystem::remove_all(test_directory);
    tap(pointer, refresh);
    lv_obj_update_layout(context.root);
    assert(std::filesystem::is_directory(test_directory));
    assert(!lv_obj_find_by_name(context.root, "nes_rom_row"));
    assert(!lv_obj_find_by_name(context.root, "nes_empty_state"));
    assert(lv_obj_get_child_count(list) == 1);
    assert(lv_obj_has_flag(error_overlay, LV_OBJ_FLAG_HIDDEN));
    assert_library_layout(context.root, width, height);
    lv_refr_now(display);
    const auto empty_screenshot = screenshot_path.parent_path() /
                                  (screenshot_path.stem().string() + "-empty" + screenshot_path.extension().string());
    assert(write_ppm(empty_screenshot.c_str(), width, height));
    assert_library_help(context.root, pointer, display, test_directory, screenshot_path, width, height);
    auto *empty_add = lv_obj_find_by_name(context.root, "nes_add_rom");
    assert(empty_add);
    assert_inside(empty_add, width, height);
    assert_not_clipped(empty_add);
    tap(pointer, empty_add);
    lv_obj_update_layout(context.root);
    auto *browser = lv_obj_find_by_name(context.root, "nes_rom_browser");
    auto *browser_cancel = lv_obj_find_by_name(context.root, "nes_browser_cancel");
    assert(browser && browser_cancel && !lv_obj_has_flag(browser, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));
    tap(pointer, browser_cancel);
    lv_obj_update_layout(context.root);
    assert(lv_obj_has_flag(browser, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN));

    // Closing the app while an alert is active must cancel its pending callback.
    const auto invalid_rom = test_directory / "Invalid ROM.nes";
    std::ofstream(invalid_rom, std::ios::binary) << "Not an iNES ROM";
    tap(pointer, refresh);
    lv_obj_update_layout(context.root);
    auto *invalid_row = lv_obj_find_by_name(context.root, "nes_rom_row");
    assert(invalid_row);
    tap(pointer, invalid_row);
    assert_error_dialog(context.root, "Invalid iNES ROM", width, height);
    descriptor->close();
    lv_tick_inc(5000);
    lv_timer_handler();
    assert(!lv_obj_find_by_name(context.root, "nes_error_overlay"));

    // Reuse the same View in a nested, small parent. The frame stays at its native
    // size, so recreation must restore its image source even when the scaler's
    // cached dimensions have not changed. No emulator is needed for this check.
    auto *nested_parent = lv_obj_create(context.root);
    lv_obj_set_size(nested_parent, 320, 400);
    lv_obj_set_style_border_width(nested_parent, 0, 0);
    lv_obj_set_style_pad_all(nested_parent, 0, 0);
    lv_obj_set_style_radius(nested_parent, 0, 0);
    lv_obj_clear_flag(nested_parent, LV_OBJ_FLAG_SCROLLABLE);
    {
        NesViewModel nested_model;
        NesView nested_view(nested_model);
        const auto assert_nested_pages = [&]() {
            lv_obj_update_layout(nested_parent);
            assert(lv_obj_get_child_count(nested_parent) == 1);
            for (const auto *name : {"nes_library", "nes_rom_browser", "nes_game",
                                     "nes_settings_panel", "nes_help_panel"}) {
                auto *page = lv_obj_find_by_name(nested_parent, name);
                assert(page);
                auto *ancestor = lv_obj_get_parent(page);
                while (ancestor && ancestor != nested_parent) ancestor = lv_obj_get_parent(ancestor);
                assert(ancestor == nested_parent);
            }
            auto *nested_image = lv_obj_find_by_name(nested_parent, "nes_frame_image");
            assert(nested_image);
            const auto *frame_source = static_cast<const lv_image_dsc_t *>(lv_image_get_src(nested_image));
            assert(frame_source && frame_source->data);
            assert(frame_source->header.magic == LV_IMAGE_HEADER_MAGIC);
            assert(frame_source->header.w == 256 && frame_source->header.h == 240);
            assert(!nested_model.game().active());
        };

        auto *nested_surface = nested_view.create(nested_parent);
        assert(nested_surface && lv_obj_get_parent(nested_surface) == nested_parent);
        assert(nested_view.create(nested_parent) == nested_surface);
        assert_nested_pages();
        nested_view.destroy();
        assert(lv_obj_get_child_count(nested_parent) == 0);
        nested_view.destroy();
        assert(lv_obj_get_child_count(nested_parent) == 0);

        nested_surface = nested_view.create(nested_parent);
        assert(nested_surface && lv_obj_get_parent(nested_surface) == nested_parent);
        assert(nested_view.create(nested_parent) == nested_surface);
        assert_nested_pages();
        nested_view.destroy();
        assert(lv_obj_get_child_count(nested_parent) == 0);
    }
    // Exercise pending timers after both the View and its ViewModel have gone.
    lv_tick_inc(5000);
    lv_timer_handler();
    assert(lv_obj_get_child_count(nested_parent) == 0);
    lv_obj_delete(nested_parent);

    lv_indev_delete(pointer);
    lv_indev_delete(keyboard);
    lv_group_set_default(nullptr);
    lv_group_delete(input_group);
    lv_display_delete(display);
    lilygo_ui_fonts_deinit();
    lv_deinit();
    std::filesystem::remove_all(temporary_root);
    return 0;
}
