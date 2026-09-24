#include "pages/nes/nes_view.hpp"

#include "components/components.hpp"
#include "emulator/nes_performance.hpp"

#include <cm0/typography.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

constexpr std::uint32_t kColorGameBackground = 0x151515;
constexpr std::uint32_t kColorScreenBorder   = 0x3a3a3a;
constexpr std::uint32_t kColorMutedText      = 0x667085;
constexpr std::uint32_t kColorGameMutedText  = 0xb8bcc4;
constexpr std::uint32_t kColorAccent         = 0xe5484d;
constexpr std::uint32_t kColorSuccess        = 0x16845b;
constexpr std::uint32_t kColorWarning        = 0xf2c94c;
constexpr std::uint32_t kColorControl        = 0x303238;
constexpr std::uint32_t kColorControlPressed = 0x4a4d55;
constexpr int kDpadGap                     = 6;
constexpr int kActionButtonGap             = 32;
constexpr int kDeleteButtonWidth           = 84;
constexpr int kSwipeThreshold              = 12;
constexpr std::uint32_t kFramePollPeriodMs  = 16;

void set_font(lv_obj_t *object, int size)
{
    if (auto *font = lilygo_ui_font_get(size)) lv_obj_set_style_text_font(object, font, 0);
}

void make_transparent(lv_obj_t *object)
{
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

std::string format_size(std::uintmax_t bytes)
{
    char value[32]{};
    if (bytes >= 1024U * 1024U) {
        std::snprintf(value, sizeof(value), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024U) {
        std::snprintf(value, sizeof(value), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(value, sizeof(value), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return value;
}

bool point_inside(const TouchPoint &point, lv_obj_t *object)
{
    if (!point.active || !object || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return false;
    lv_area_t area{};
    lv_obj_get_coords(object, &area);
    return point.x >= area.x1 && point.x <= area.x2 && point.y >= area.y1 && point.y <= area.y2;
}

}  // namespace

NesView::NesView(NesViewModel &view_model) noexcept : view_model_(view_model)
{
}

lv_obj_t *NesView::create(lv_obj_t *parent, const char *application_name)
{
    surface_ = components::create_page(parent);
    lv_obj_add_event_cb(surface_, size_changed_event, LV_EVENT_SIZE_CHANGED, this);

    header_         = components::create_header(surface_, application_name);
    refresh_button_ = create_icon_button(header_, LV_SYMBOL_REFRESH, refresh_event);
    lv_obj_set_name_static(refresh_button_, "nes_refresh");
    lv_obj_align(refresh_button_, LV_ALIGN_RIGHT_MID, 0, 0);

    body_ = lv_obj_create(surface_);
    lv_obj_set_width(body_, LV_PCT(100));
    lv_obj_set_flex_grow(body_, 1);
    make_transparent(body_);

    create_library();
    create_game();
    rebuild_rom_list();
    show_game(false);

    resize_frame(NesEmulator::frame_width, NesEmulator::frame_height);

    lv_obj_update_layout(surface_);
    apply_layout();
    if (auto *display = lv_obj_get_display(surface_)) {
        multitouch_.start(lv_display_get_horizontal_resolution(display), lv_display_get_vertical_resolution(display));
    }
    timer_ = lv_timer_create(timer_event, kFramePollPeriodMs, this);
    return surface_;
}

void NesView::destroy() noexcept
{
    if (timer_) lv_timer_delete(timer_);
    timer_ = nullptr;
    multitouch_.stop();
    view_model_.stop_rom();
    lvgl_pressed_mask_       = 0;
    multitouch_pressed_mask_ = 0;
    sync_controls();
    if (surface_) lv_obj_delete(surface_);
#if LV_USE_DRAW_OPENGLES
    gpu_frame_.reset();
#endif
    surface_           = nullptr;
    header_            = nullptr;
    refresh_button_    = nullptr;
    body_              = nullptr;
    library_panel_     = nullptr;
    library_summary_   = nullptr;
    library_list_      = nullptr;
    directory_label_   = nullptr;
    status_label_      = nullptr;
    game_panel_        = nullptr;
    game_content_      = nullptr;
    game_display_      = nullptr;
    frame_panel_       = nullptr;
    frame_image_       = nullptr;
    frame_placeholder_ = nullptr;
    game_status_label_ = nullptr;
    system_controls_   = nullptr;
    dpad_              = nullptr;
    action_buttons_    = nullptr;
    control_buttons_.fill(nullptr);
    rom_contexts_.clear();
}

lv_obj_t *NesView::create_icon_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t callback)
{
    auto *button = lv_button_create(parent);
    lv_obj_set_size(button, 52, 52);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(components::color_action), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);

    auto *label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    set_font(label, 22);
    lv_obj_set_style_text_color(label, lv_color_hex(components::color_container_background), 0);
    lv_obj_center(label);
    return button;
}

void NesView::create_library()
{
    library_panel_ = lv_obj_create(body_);
    lv_obj_set_name_static(library_panel_, "nes_library");
    lv_obj_set_size(library_panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(library_panel_, 0, 0);
    lv_obj_set_style_radius(library_panel_, 0, 0);
    lv_obj_set_style_bg_color(library_panel_, lv_color_hex(components::color_page_background), 0);
    lv_obj_set_style_bg_opa(library_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(library_panel_, 0, 0);
    lv_obj_clear_flag(library_panel_, LV_OBJ_FLAG_SCROLLABLE);

    library_summary_ = lv_obj_create(library_panel_);
    lv_obj_set_style_border_width(library_summary_, 0, 0);
    lv_obj_set_style_radius(library_summary_, 0, 0);
    lv_obj_set_style_bg_color(library_summary_, lv_color_hex(components::color_container_background), 0);
    lv_obj_set_style_bg_opa(library_summary_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(library_summary_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(library_summary_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(library_summary_, 8, 0);

    auto *title = lv_label_create(library_summary_);
    lv_label_set_text(title, "ROM Library");
    set_font(title, 28);
    lv_obj_set_style_text_color(title, lv_color_hex(components::color_container_title), 0);

    auto *count = lv_label_create(library_summary_);
    lv_obj_set_name_static(count, "nes_rom_count");
    set_font(count, 22);
    lv_obj_set_style_text_color(count, lv_color_hex(kColorAccent), 0);
    lv_label_bind_text(count, view_model_.rom_count_subject(), "ROMs: %d");

    directory_label_ = lv_label_create(library_summary_);
    lv_label_set_text(directory_label_, view_model_.rom_directory().c_str());
    set_font(directory_label_, 14);
    lv_obj_set_width(directory_label_, LV_PCT(100));
    lv_label_set_long_mode(directory_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(directory_label_, lv_color_hex(kColorMutedText), 0);

    status_label_ = lv_label_create(library_summary_);
    lv_obj_set_name_static(status_label_, "nes_status");
    set_font(status_label_, 16);
    lv_obj_set_width(status_label_, LV_PCT(100));
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);

    library_list_ = lv_obj_create(library_panel_);
    lv_obj_set_name_static(library_list_, "nes_rom_list");
    lv_obj_set_style_border_width(library_list_, 0, 0);
    lv_obj_set_style_radius(library_list_, 0, 0);
    lv_obj_set_style_bg_opa(library_list_, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(library_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(library_list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(library_list_, 10, 0);
    lv_obj_set_scroll_dir(library_list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(library_list_, LV_SCROLLBAR_MODE_AUTO);
}

void NesView::rebuild_rom_list()
{
    if (!library_list_) return;
    lv_obj_clean(library_list_);
    const auto &roms = view_model_.roms();
    rom_contexts_.clear();
    rom_contexts_.resize(roms.size());

    if (roms.empty()) {
        auto *empty = lv_obj_create(library_list_);
        lv_obj_set_name_static(empty, "nes_empty_state");
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_height(empty, 190);
        make_transparent(empty);

        auto *icon = lv_label_create(empty);
        lv_label_set_text(icon, LV_SYMBOL_DIRECTORY);
        set_font(icon, 36);
        lv_obj_set_style_text_color(icon, lv_color_hex(kColorMutedText), 0);
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, -38);

        auto *message = lv_label_create(empty);
        lv_label_set_text(message, "No .nes files found");
        set_font(message, 22);
        lv_obj_set_style_text_color(message, lv_color_hex(components::color_container_title), 0);
        lv_obj_align(message, LV_ALIGN_CENTER, 0, 8);

        auto *path = lv_label_create(empty);
        lv_label_set_text(path, view_model_.rom_directory().c_str());
        set_font(path, 14);
        lv_obj_set_width(path, LV_PCT(90));
        lv_label_set_long_mode(path, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(path, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(path, lv_color_hex(kColorMutedText), 0);
        lv_obj_align(path, LV_ALIGN_CENTER, 0, 48);
    }

    for (std::size_t index = 0; index < roms.size(); ++index) {
        const auto &rom      = roms[index];
        rom_contexts_[index] = {this, index};
        auto &context        = rom_contexts_[index];
        auto *item           = lv_obj_create(library_list_);
        if (index == 0) lv_obj_set_name_static(item, "nes_rom_item");
        lv_obj_set_width(item, LV_PCT(100));
        lv_obj_set_height(item, LV_SIZE_CONTENT);
        make_transparent(item);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_CLICKABLE);

        auto *remove = lv_button_create(item);
        if (index == 0) lv_obj_set_name_static(remove, "nes_rom_delete");
        lv_obj_set_size(remove, kDeleteButtonWidth, LV_PCT(100));
        lv_obj_align(remove, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_radius(remove, 6, 0);
        lv_obj_set_style_bg_color(remove, lv_color_hex(kColorAccent), 0);
        lv_obj_set_style_shadow_width(remove, 0, 0);
        lv_obj_add_flag(remove, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(remove, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(remove, delete_rom_event, LV_EVENT_PRESSED, &context);
        lv_obj_add_event_cb(remove, delete_rom_event, LV_EVENT_PRESSING, &context);
        lv_obj_add_event_cb(remove, delete_rom_event, LV_EVENT_RELEASED, &context);
        lv_obj_add_event_cb(remove, delete_rom_event, LV_EVENT_PRESS_LOST, &context);
        lv_obj_add_event_cb(remove, delete_rom_event, LV_EVENT_CLICKED, &context);
        auto *remove_icon = lv_label_create(remove);
        lv_label_set_text(remove_icon, LV_SYMBOL_TRASH);
        set_font(remove_icon, 24);
        lv_obj_set_style_text_color(remove_icon, lv_color_hex(0xffffff), 0);
        lv_obj_center(remove_icon);

        auto *row = lv_button_create(item);
        if (index == 0) lv_obj_set_name_static(row, "nes_rom_row");
        context.row    = row;
        context.remove = remove;
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(row, 88, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(components::color_border), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(components::color_container_background), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xf7f7f8), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 14, 0);
        lv_obj_set_style_pad_column(row, 14, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(row, rom_event, LV_EVENT_PRESSED, &context);
        lv_obj_add_event_cb(row, rom_event, LV_EVENT_PRESSING, &context);
        lv_obj_add_event_cb(row, rom_event, LV_EVENT_RELEASED, &context);
        lv_obj_add_event_cb(row, rom_event, LV_EVENT_PRESS_LOST, &context);
        lv_obj_add_event_cb(row, rom_event, LV_EVENT_CLICKED, &context);

        auto *rom_icon = lv_obj_create(row);
        if (index == 0) lv_obj_set_name_static(rom_icon, "nes_rom_icon");
        lv_obj_set_size(rom_icon, 52, 52);
        lv_obj_set_style_radius(rom_icon, 6, 0);
        lv_obj_set_style_border_width(rom_icon, 0, 0);
        lv_obj_set_style_bg_color(rom_icon, lv_color_hex(rom.valid_header ? kColorAccent : kColorMutedText), 0);
        lv_obj_set_style_bg_opa(rom_icon, LV_OPA_COVER, 0);
        lv_obj_clear_flag(rom_icon, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(rom_icon, LV_OBJ_FLAG_CLICKABLE);
        auto *rom_icon_label = lv_label_create(rom_icon);
        lv_label_set_text(rom_icon_label, "NES");
        set_font(rom_icon_label, 14);
        lv_obj_set_style_text_color(rom_icon_label, lv_color_hex(0xffffff), 0);
        lv_obj_center(rom_icon_label);

        auto *text_column = lv_obj_create(row);
        if (index == 0) lv_obj_set_name_static(text_column, "nes_rom_text");
        lv_obj_set_size(text_column, 0, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(text_column, 1);
        make_transparent(text_column);
        lv_obj_clear_flag(text_column, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_flow(text_column, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(text_column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(text_column, 4, 0);

        auto *rom_title = lv_label_create(text_column);
        if (index == 0) lv_obj_set_name_static(rom_title, "nes_rom_title");
        lv_label_set_text(rom_title, rom.title.c_str());
        set_font(rom_title, 18);
        lv_obj_set_width(rom_title, LV_PCT(100));
        lv_label_set_long_mode(rom_title, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(rom_title, lv_color_hex(components::color_text), 0);

        const std::string details = rom.metadata + "  |  " + format_size(rom.size);
        auto *rom_metadata        = lv_label_create(text_column);
        if (index == 0) lv_obj_set_name_static(rom_metadata, "nes_rom_metadata");
        lv_label_set_text(rom_metadata, details.c_str());
        set_font(rom_metadata, 14);
        lv_obj_set_width(rom_metadata, LV_PCT(100));
        lv_label_set_long_mode(rom_metadata, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(rom_metadata, lv_color_hex(kColorMutedText), 0);

    }
    update_status();
}

void NesView::set_rom_offset(RomContext &context, int32_t offset)
{
    lv_obj_set_style_translate_x(context.row, offset, 0);
    if (offset < 0) {
        lv_obj_clear_flag(context.remove, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(context.remove, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *NesView::create_control_button(lv_obj_t *parent, const char *text, NesButton button, std::uint32_t color,
                                         std::uint32_t text_color)
{
    static const char *const names[] = {"nes_control_up",    "nes_control_down",   "nes_control_left",
                                        "nes_control_right", "nes_control_select", "nes_control_start",
                                        "nes_control_a",     "nes_control_b"};
    const auto index                 = static_cast<std::size_t>(button);
    control_contexts_[index]         = {this, button};
    auto *control                    = lv_button_create(parent);
    lv_obj_set_name_static(control, names[index]);
    lv_obj_set_style_radius(control, 6, 0);
    lv_obj_set_style_border_width(control, 1, 0);
    lv_obj_set_style_border_color(control, lv_color_hex(kColorScreenBorder), 0);
    lv_obj_set_style_bg_color(control, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(control, lv_color_hex(kColorControlPressed), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(control, 0, 0);
    lv_obj_add_event_cb(control, control_event, LV_EVENT_PRESSED, &control_contexts_[index]);
    lv_obj_add_event_cb(control, control_event, LV_EVENT_RELEASED, &control_contexts_[index]);
    lv_obj_add_event_cb(control, control_event, LV_EVENT_PRESS_LOST, &control_contexts_[index]);
    auto *label = lv_label_create(control);
    lv_label_set_text(label, text);
    set_font(label, 18);
    lv_obj_set_style_text_color(label, lv_color_hex(text_color), 0);
    lv_obj_center(label);
    control_buttons_[index] = control;
    return control;
}

void NesView::create_game()
{
    game_panel_ = lv_obj_create(body_);
    lv_obj_set_name_static(game_panel_, "nes_game");
    lv_obj_set_size(game_panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(game_panel_, 0, 0);
    lv_obj_set_style_radius(game_panel_, 0, 0);
    lv_obj_set_style_bg_color(game_panel_, lv_color_hex(kColorGameBackground), 0);
    lv_obj_set_style_bg_opa(game_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(game_panel_, 0, 0);
    lv_obj_clear_flag(game_panel_, LV_OBJ_FLAG_SCROLLABLE);

    game_content_ = lv_obj_create(game_panel_);
    lv_obj_set_size(game_content_, LV_PCT(100), LV_PCT(100));
    make_transparent(game_content_);

    dpad_ = lv_obj_create(game_content_);
    make_transparent(dpad_);
    lv_obj_set_size(dpad_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(dpad_, kDpadGap, 0);
    lv_obj_set_style_pad_column(dpad_, kDpadGap, 0);
    lv_obj_set_layout(dpad_, LV_LAYOUT_GRID);
    auto *up    = create_control_button(dpad_, LV_SYMBOL_UP, NesButton::up, kColorControl);
    auto *down  = create_control_button(dpad_, LV_SYMBOL_DOWN, NesButton::down, kColorControl);
    auto *left  = create_control_button(dpad_, LV_SYMBOL_LEFT, NesButton::left, kColorControl);
    auto *right = create_control_button(dpad_, LV_SYMBOL_RIGHT, NesButton::right, kColorControl);
    lv_obj_set_grid_cell(up, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(left, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_grid_cell(right, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_grid_cell(down, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 2, 1);

    game_display_ = lv_obj_create(game_content_);
    make_transparent(game_display_);
    lv_obj_set_flex_flow(game_display_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(game_display_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(game_display_, 8, 0);

    frame_panel_ = lv_obj_create(game_display_);
    lv_obj_set_name_static(frame_panel_, "nes_frame");
    lv_obj_set_style_radius(frame_panel_, 4, 0);
    lv_obj_set_style_border_width(frame_panel_, 2, 0);
    lv_obj_set_style_border_color(frame_panel_, lv_color_hex(kColorScreenBorder), 0);
    lv_obj_set_style_bg_color(frame_panel_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(frame_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(frame_panel_, 0, 0);
    lv_obj_clear_flag(frame_panel_, LV_OBJ_FLAG_SCROLLABLE);

#if LV_USE_DRAW_OPENGLES
    frame_image_ = lv_3dtexture_create(frame_panel_);
    make_transparent(frame_image_);
    if (gpu_frame_.initialize()) {
        lv_3dtexture_set_src(frame_image_, gpu_frame_.texture_id());
        lv_3dtexture_set_flip(frame_image_, false, false);
    }
#else
    frame_image_ = lv_image_create(frame_panel_);
    lv_image_set_antialias(frame_image_, false);
#endif
    lv_obj_set_name_static(frame_image_, "nes_frame_image");
    lv_obj_set_size(frame_image_, NesEmulator::frame_width, NesEmulator::frame_height);
    lv_obj_add_flag(frame_image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(frame_image_);

    frame_placeholder_ = lv_label_create(frame_panel_);
    lv_label_set_text(frame_placeholder_, "NES");
    set_font(frame_placeholder_, 36);
    lv_obj_set_style_text_color(frame_placeholder_, lv_color_hex(kColorAccent), 0);
    lv_obj_center(frame_placeholder_);

    game_status_label_ = lv_label_create(game_display_);
    lv_obj_set_name_static(game_status_label_, "nes_game_status");
    set_font(game_status_label_, 14);
    lv_label_set_long_mode(game_status_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(game_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(game_status_label_, lv_color_hex(kColorGameMutedText), 0);

    system_controls_ = lv_obj_create(game_display_);
    make_transparent(system_controls_);
    lv_obj_set_height(system_controls_, 48);
    lv_obj_set_flex_flow(system_controls_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(system_controls_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(system_controls_, 10, 0);

    auto *select = create_control_button(system_controls_, "Select", NesButton::select, kColorControl);
    auto *start  = create_control_button(system_controls_, "Start", NesButton::start, kColorControl);
    lv_obj_set_size(select, 108, 44);
    lv_obj_set_size(start, 108, 44);
    auto *stop = lv_button_create(system_controls_);
    lv_obj_set_name_static(stop, "nes_stop");
    lv_obj_set_size(stop, 52, 44);
    lv_obj_set_style_radius(stop, 6, 0);
    lv_obj_set_style_bg_color(stop, lv_color_hex(kColorAccent), 0);
    lv_obj_set_style_shadow_width(stop, 0, 0);
    lv_obj_add_event_cb(stop, stop_event, LV_EVENT_CLICKED, this);
    auto *stop_label = lv_label_create(stop);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP);
    set_font(stop_label, 18);
    lv_obj_set_style_text_color(stop_label, lv_color_hex(0xffffff), 0);
    lv_obj_center(stop_label);

    action_buttons_ = lv_obj_create(game_content_);
    make_transparent(action_buttons_);
    lv_obj_set_flex_flow(action_buttons_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(action_buttons_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(action_buttons_, kActionButtonGap, 0);
    auto *button_b = create_control_button(action_buttons_, "B", NesButton::b, kColorWarning, 0x202124);
    auto *button_a = create_control_button(action_buttons_, "A", NesButton::a, kColorAccent);
    lv_obj_set_style_radius(button_b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(button_a, LV_RADIUS_CIRCLE, 0);
}

void NesView::show_game(bool visible)
{
    if (!library_panel_ || !game_panel_) return;
    if (visible) {
        lv_obj_add_flag(library_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(game_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(refresh_button_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(library_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(game_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(refresh_button_, LV_OBJ_FLAG_HIDDEN);
        lvgl_pressed_mask_       = 0;
        multitouch_pressed_mask_ = 0;
        sync_controls();
    }
    update_status();
}

void NesView::update_status()
{
#if LV_USE_DRAW_OPENGLES
    if (!gpu_frame_.ready()) {
        if (status_label_) {
            lv_label_set_text(status_label_, "GPU unavailable; see application log");
            lv_obj_set_style_text_color(status_label_, lv_color_hex(kColorAccent), 0);
        }
        if (game_status_label_) lv_label_set_text(game_status_label_, "GPU unavailable; see application log");
        return;
    }
#endif
    if (status_label_) {
        lv_label_set_text(status_label_, view_model_.status().c_str());
        const auto color = view_model_.state() == NesUiState::error
                               ? kColorAccent
                               : (view_model_.roms().empty() ? kColorMutedText : kColorSuccess);
        lv_obj_set_style_text_color(status_label_, lv_color_hex(color), 0);
    }
    if (game_status_label_) lv_label_set_text(game_status_label_, view_model_.status().c_str());
}

void NesView::sync_controls() noexcept
{
    const auto combined = lvgl_pressed_mask_ | multitouch_pressed_mask_;
    const auto changed  = combined ^ sent_pressed_mask_;
    for (unsigned bit = 0; bit < 8; ++bit) {
        if ((changed & (1U << bit)) != 0) {
            view_model_.set_button(static_cast<NesButton>(bit), (combined & (1U << bit)) != 0);
        }
    }
    sent_pressed_mask_ = combined;
}

void NesView::update_multitouch()
{
    if (!game_panel_ || lv_obj_has_flag(game_panel_, LV_OBJ_FLAG_HIDDEN)) return;
    std::uint32_t mask = 0;
    for (const auto &point : multitouch_.points()) {
        for (unsigned bit = 0; bit < control_buttons_.size(); ++bit) {
            if (point_inside(point, control_buttons_[bit])) mask |= 1U << bit;
        }
    }
    if (mask != multitouch_pressed_mask_) {
        multitouch_pressed_mask_ = mask;
        sync_controls();
    }
}

void NesView::apply_layout() noexcept
{
    if (!surface_ || !body_) return;
    lv_obj_update_layout(surface_);
    const int width       = lv_obj_get_content_width(surface_);
    const int body_height = lv_obj_get_content_height(body_);
    const bool landscape  = width >= 800 && width > body_height;

    lv_obj_set_height(header_, landscape ? 72 : 88);
    lv_obj_set_style_pad_hor(header_, landscape ? 22 : 28, 0);
    lv_obj_set_style_pad_ver(header_, landscape ? 10 : 18, 0);
    lv_obj_set_size(refresh_button_, landscape ? 48 : 52, landscape ? 48 : 52);

    lv_obj_set_flex_flow(library_panel_, landscape ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(library_panel_, landscape ? 18 : 16, 0);
    lv_obj_set_style_pad_row(library_panel_, 14, 0);
    lv_obj_set_style_pad_column(library_panel_, 18, 0);
    if (landscape) {
        lv_obj_set_size(library_summary_, 300, LV_PCT(100));
        lv_obj_set_height(library_list_, LV_PCT(100));
        lv_obj_set_flex_grow(library_list_, 1);
    } else {
        lv_obj_set_size(library_summary_, LV_PCT(100), 164);
        lv_obj_set_width(library_list_, LV_PCT(100));
        lv_obj_set_flex_grow(library_list_, 1);
    }
    lv_obj_set_style_pad_all(library_summary_, landscape ? 22 : 18, 0);
    lv_obj_set_style_pad_all(library_list_, landscape ? 4 : 0, 0);

    static const int32_t landscape_columns[] = {LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_rows[]    = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t portrait_columns[]  = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t portrait_rows[]     = {LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t dpad_landscape[]    = {68, 68, 68, LV_GRID_TEMPLATE_LAST};
    static const int32_t dpad_portrait[]     = {78, 78, 78, LV_GRID_TEMPLATE_LAST};

    lv_obj_set_grid_dsc_array(game_content_, landscape ? landscape_columns : portrait_columns,
                              landscape ? landscape_rows : portrait_rows);
    lv_obj_set_style_pad_all(game_content_, landscape ? 12 : 18, 0);
    lv_obj_set_style_pad_row(game_content_, landscape ? 0 : 18, 0);
    lv_obj_set_style_pad_column(game_content_, landscape ? 14 : 18, 0);

    if (landscape) {
        lv_obj_set_grid_cell(dpad_, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(game_display_, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(action_buttons_, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    } else {
        lv_obj_set_grid_cell(game_display_, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_START, 0, 1);
        lv_obj_set_grid_cell(dpad_, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 1, 1);
        lv_obj_set_grid_cell(action_buttons_, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 1, 1);
    }

    lv_obj_set_grid_dsc_array(dpad_, landscape ? dpad_landscape : dpad_portrait,
                              landscape ? dpad_landscape : dpad_portrait);

    const int action_size = landscape ? 82 : 94;
    lv_obj_set_size(action_buttons_, action_size * 2 + kActionButtonGap, landscape ? 170 : 234);
    lv_obj_set_size(control_buttons_[static_cast<std::size_t>(NesButton::a)], action_size, action_size);
    lv_obj_set_size(control_buttons_[static_cast<std::size_t>(NesButton::b)], action_size, action_size);

    int frame_width = std::min(width - 40, 528);
    if (landscape) {
        const int available_height = std::max(260, body_height - 104);
        const int frame_height     = std::min(390, available_height);
        frame_width                = frame_height * NesEmulator::frame_width / NesEmulator::frame_height;
    }
    int frame_height = frame_width * NesEmulator::frame_height / NesEmulator::frame_width;
    if (landscape && frame_height > body_height - 104) {
        frame_height = body_height - 104;
        frame_width  = frame_height * NesEmulator::frame_width / NesEmulator::frame_height;
    }
    frame_width  = std::max(frame_width, 256);
    frame_height = std::max(frame_height, 240);

    lv_obj_set_size(frame_panel_, frame_width, frame_height);
    const auto scale = static_cast<std::uint32_t>(std::min(frame_width * LV_SCALE_NONE / NesEmulator::frame_width,
                                                           frame_height * LV_SCALE_NONE / NesEmulator::frame_height));
    resize_frame(NesEmulator::frame_width * static_cast<int>(scale) / LV_SCALE_NONE,
                 NesEmulator::frame_height * static_cast<int>(scale) / LV_SCALE_NONE);
    lv_obj_set_width(game_status_label_, frame_width);
    lv_obj_set_width(system_controls_, frame_width);
    lv_obj_set_size(game_display_, frame_width, frame_height + 80);

    if (auto *display = lv_obj_get_display(surface_)) {
        multitouch_.set_display_size(lv_display_get_horizontal_resolution(display),
                                     lv_display_get_vertical_resolution(display));
    }
}

void NesView::resize_frame(int width, int height)
{
#if LV_USE_DRAW_OPENGLES
    lv_obj_set_size(frame_image_, width, height);
#else
    if (!scaled_frame_.resize(width, height)) return;
    scaled_frame_.update(frame_buffer_);
    frame_descriptor_.header.magic  = LV_IMAGE_HEADER_MAGIC;
    frame_descriptor_.header.cf     = LV_COLOR_FORMAT_XRGB8888;
    frame_descriptor_.header.flags  = LV_IMAGE_FLAGS_MODIFIABLE;
    frame_descriptor_.header.w      = width;
    frame_descriptor_.header.h      = height;
    frame_descriptor_.header.stride = width * sizeof(std::uint32_t);
    frame_descriptor_.data_size     = scaled_frame_.pixels().size() * sizeof(std::uint32_t);
    frame_descriptor_.data          = reinterpret_cast<const std::uint8_t *>(scaled_frame_.pixels().data());
    lv_image_set_src(frame_image_, &frame_descriptor_);
    lv_obj_set_size(frame_image_, width, height);
#endif
}

void NesView::size_changed_event(lv_event_t *event)
{
    static_cast<NesView *>(lv_event_get_user_data(event))->apply_layout();
}

void NesView::refresh_event(lv_event_t *event)
{
    auto *view = static_cast<NesView *>(lv_event_get_user_data(event));
    view->view_model_.refresh_library();
    view->rebuild_rom_list();
}

void NesView::rom_event(lv_event_t *event)
{
    auto *context = static_cast<RomContext *>(lv_event_get_user_data(event));
    if (!context || !context->view) return;
    auto *view       = context->view;
    const auto code  = lv_event_get_code(event);
    auto *input      = lv_event_get_indev(event);
    if (code == LV_EVENT_PRESSED) {
        for (auto &other : view->rom_contexts_) {
            if (&other != context) view->set_rom_offset(other, 0);
        }
        context->initial_offset = lv_obj_get_style_translate_x(context->row, LV_PART_MAIN);
        context->tracking       = input && lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER;
        context->moved          = false;
        context->horizontal     = false;
        if (context->tracking) lv_indev_get_point(input, &context->press_point);
    } else if (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) {
        if (context->tracking && input) {
            lv_point_t point{};
            lv_indev_get_point(input, &point);
            const auto dx = point.x - context->press_point.x;
            const auto dy = point.y - context->press_point.y;
            if (!context->moved && (std::abs(dx) >= kSwipeThreshold || std::abs(dy) >= kSwipeThreshold)) {
                context->moved      = true;
                context->horizontal = std::abs(dx) > std::abs(dy);
            }
            if (context->horizontal) {
                view->set_rom_offset(*context, std::clamp(context->initial_offset + dx, -kDeleteButtonWidth, 0));
            }
        }
        if (code == LV_EVENT_RELEASED) {
            const auto offset = lv_obj_get_style_translate_x(context->row, LV_PART_MAIN);
            view->set_rom_offset(*context, offset <= -kDeleteButtonWidth / 2 ? -kDeleteButtonWidth : 0);
            context->tracking = false;
        }
    } else if (code == LV_EVENT_PRESS_LOST) {
        view->set_rom_offset(*context, context->initial_offset);
        context->tracking = false;
        context->moved    = true;
    } else if (code == LV_EVENT_CLICKED && !context->moved) {
        view->set_rom_offset(*context, 0);
#if LV_USE_DRAW_OPENGLES
        if (!view->gpu_frame_.ready()) {
            view->update_status();
            return;
        }
#endif
        if (view->view_model_.start_rom(context->index)) {
            view->show_game(true);
        } else {
            view->update_status();
        }
    }
}

void NesView::delete_rom_event(lv_event_t *event)
{
    auto *context = static_cast<RomContext *>(lv_event_get_user_data(event));
    if (!context || !context->view) return;
    const auto code = lv_event_get_code(event);
    auto *input     = lv_event_get_indev(event);
    if (code == LV_EVENT_PRESSED) {
        context->tracking = input && lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER;
        context->moved    = false;
        if (context->tracking) lv_indev_get_point(input, &context->press_point);
    } else if (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) {
        if (context->tracking && input) {
            lv_point_t point{};
            lv_indev_get_point(input, &point);
            context->moved = context->moved || std::abs(point.x - context->press_point.x) >= kSwipeThreshold ||
                             std::abs(point.y - context->press_point.y) >= kSwipeThreshold;
        }
        if (code == LV_EVENT_RELEASED) context->tracking = false;
    } else if (code == LV_EVENT_PRESS_LOST) {
        context->tracking = false;
        context->moved    = true;
    } else if (code == LV_EVENT_CLICKED && !context->moved) {
        auto *view = context->view;
        if (view->view_model_.delete_rom(context->index)) {
            view->rebuild_rom_list();
        } else {
            view->update_status();
        }
    }
}

void NesView::stop_event(lv_event_t *event)
{
    auto *view = static_cast<NesView *>(lv_event_get_user_data(event));
    view->view_model_.stop_rom();
    view->update_status();
}

void NesView::control_event(lv_event_t *event)
{
    auto *context = static_cast<ControlContext *>(lv_event_get_user_data(event));
    if (!context || !context->view) return;
    const auto bit  = static_cast<unsigned>(context->button);
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        context->view->lvgl_pressed_mask_ |= 1U << bit;
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        context->view->lvgl_pressed_mask_ &= ~(1U << bit);
    }
    context->view->sync_controls();
}

void NesView::timer_event(lv_timer_t *timer)
{
    auto *view = static_cast<NesView *>(lv_timer_get_user_data(timer));
    nes_performance::report_if_due();
    view->update_multitouch();
    if (!lv_obj_has_flag(view->game_panel_, LV_OBJ_FLAG_HIDDEN) &&
        view->view_model_.copy_latest_frame(view->frame_buffer_.data(), view->frame_buffer_.size())) {
#if LV_USE_DRAW_OPENGLES
        bool uploaded;
        {
            nes_performance::Scope measured(nes_performance::Stage::gpu_upload);
            uploaded = view->gpu_frame_.update(view->frame_buffer_);
        }
        if (!uploaded) {
            lv_obj_add_flag(view->frame_image_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(view->frame_placeholder_, LV_OBJ_FLAG_HIDDEN);
            view->view_model_.stop_rom();
            view->show_game(false);
            return;
        }
#else
        {
            nes_performance::Scope measured(nes_performance::Stage::frame_prepare);
            view->scaled_frame_.update(view->frame_buffer_);
        }
#endif
        if (lv_obj_has_flag(view->frame_image_, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(view->frame_image_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(view->frame_placeholder_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_invalidate(view->frame_image_);
        // Present new frames without waiting for the display's default 33 ms timer.
        if (auto *refresh = lv_display_get_refr_timer(lv_obj_get_display(view->frame_image_))) {
            lv_timer_ready(refresh);
        }
    }
    if (view->view_model_.poll()) {
        view->show_game(false);
        lv_obj_add_flag(view->frame_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(view->frame_placeholder_, LV_OBJ_FLAG_HIDDEN);
    }
}
