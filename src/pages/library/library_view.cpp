#include "pages/library/library_view.hpp"

#include "components/components.hpp"
#include "components/page_helpers.hpp"

#include <cm0/typography.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
using namespace page_ui;
constexpr int kDeleteButtonWidth = 84;
constexpr int kDeleteButtonGap = 12;
constexpr int kDeleteRevealWidth = kDeleteButtonWidth + kDeleteButtonGap;
constexpr int kSwipeThreshold = 12;

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

}  // namespace

LibraryView::LibraryView(LibraryViewModel &view_model, const NesPageCallbacks &callbacks) noexcept
    : view_model_(view_model), callbacks_(callbacks)
{
}

lv_obj_t *LibraryView::create_icon_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t callback)
{
    auto *button = lv_button_create(parent);
    lv_obj_set_size(button, control_size(48), control_size(48));
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(components::color_tinted_surface), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(components::color_action), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(button, lv_color_hex(components::color_action), 0);
    lv_obj_set_style_text_color(button, lv_color_hex(components::color_container_background), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);

    auto *label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    set_font(label, control_size(22));
    lv_obj_center(label);
    return button;
}

lv_obj_t *LibraryView::create(lv_obj_t *parent)
{
    if (library_panel_) return library_panel_;
    library_panel_ = lv_obj_create(parent);
    lv_obj_set_name_static(library_panel_, "nes_library");
    lv_obj_set_size(library_panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(library_panel_, 0, 0);
    lv_obj_set_style_radius(library_panel_, 0, 0);
    lv_obj_set_style_bg_color(library_panel_, lv_color_hex(components::color_page_background), 0);
    lv_obj_set_style_bg_opa(library_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(library_panel_, 16, 0);
    lv_obj_set_style_pad_bottom(library_panel_, kBottomInset, 0);
    lv_obj_set_style_pad_row(library_panel_, 10, 0);
    lv_obj_set_flex_flow(library_panel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(library_panel_, LV_OBJ_FLAG_SCROLLABLE);

    library_list_ = lv_obj_create(library_panel_);
    lv_obj_set_name_static(library_list_, "nes_rom_list");
    lv_obj_set_size(library_list_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(library_list_, 1);
    lv_obj_set_style_border_width(library_list_, 0, 0);
    lv_obj_set_style_radius(library_list_, 0, 0);
    lv_obj_set_style_bg_opa(library_list_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(library_list_, 0, 0);
    lv_obj_set_style_pad_row(library_list_, 12, 0);
    lv_obj_set_flex_flow(library_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(library_list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(library_list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(library_list_, LV_SCROLLBAR_MODE_AUTO);

    library_toolbar_ = lv_obj_create(library_panel_);
    lv_obj_set_name_static(library_toolbar_, "nes_library_toolbar");
    make_transparent(library_toolbar_);
    lv_obj_set_size(library_toolbar_, LV_PCT(100), control_size(56));
    lv_obj_set_flex_flow(library_toolbar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(library_toolbar_, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(library_toolbar_, control_size(10), 0);

    refresh_button_ = create_icon_button(library_toolbar_, LV_SYMBOL_REFRESH, refresh_event);
    lv_obj_set_name_static(refresh_button_, "nes_refresh");

    settings_button_ = create_icon_button(library_toolbar_, LV_SYMBOL_SETTINGS, settings_event);
    lv_obj_set_name_static(settings_button_, "nes_settings");

    help_button_ = create_icon_button(library_toolbar_, "?", help_event);
    lv_obj_set_name_static(help_button_, "nes_help");
    return library_panel_;
}

void LibraryView::rebuild_rom_list()
{
    if (!library_list_) return;
    lv_obj_clean(library_list_);
    const auto &roms = view_model_.roms();
    rom_contexts_.clear();
    rom_contexts_.resize(roms.size());

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
        lv_obj_set_style_radius(remove, 14, 0);
        lv_obj_set_style_bg_color(remove, lv_color_hex(components::color_destructive), 0);
        lv_obj_set_style_bg_color(remove, lv_color_hex(0xd70015), LV_STATE_PRESSED);
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
        lv_obj_set_style_radius(row, 16, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(components::color_border), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(components::color_container_background), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(components::color_tinted_surface), LV_STATE_PRESSED);
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
        lv_obj_set_style_radius(rom_icon, 14, 0);
        lv_obj_set_style_border_width(rom_icon, 0, 0);
        lv_obj_set_style_bg_color(rom_icon, lv_color_hex(rom.valid_header ? components::color_action : components::color_secondary_text), 0);
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
        lv_obj_set_style_text_color(rom_metadata, lv_color_hex(components::color_secondary_text), 0);

    }

    auto *add = components::create_button(library_list_, LV_SYMBOL_PLUS "  Add ROM", add_rom_event, this);
    lv_obj_set_name_static(add, "nes_add_rom");
    lv_obj_set_size(add, LV_PCT(100), 52);
    set_font(lv_obj_get_child(add, 0), 18);

    callbacks_.state_changed();
}

void LibraryView::set_rom_offset(RomContext &context, int32_t offset)
{
    lv_obj_set_style_translate_x(context.row, offset, 0);
    if (offset < 0) {
        lv_obj_clear_flag(context.remove, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(context.remove, LV_OBJ_FLAG_HIDDEN);
    }
}

void LibraryView::rom_event(lv_event_t *event)
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
                view->set_rom_offset(*context, std::clamp(context->initial_offset + dx, -kDeleteRevealWidth, 0));
            }
        }
        if (code == LV_EVENT_RELEASED) {
            const auto offset = lv_obj_get_style_translate_x(context->row, LV_PART_MAIN);
            view->set_rom_offset(*context, offset <= -kDeleteRevealWidth / 2 ? -kDeleteRevealWidth : 0);
            context->tracking = false;
        }
    } else if (code == LV_EVENT_PRESS_LOST) {
        view->set_rom_offset(*context, context->initial_offset);
        context->tracking = false;
        context->moved    = true;
    } else if (code == LV_EVENT_CLICKED && !context->moved) {
        view->set_rom_offset(*context, 0);
        if (!view->callbacks_.can_start_game()) return;
        if (view->view_model_.start_rom(context->index)) {
            view->callbacks_.navigate(NesPage::game);
        } else {
            view->callbacks_.state_changed();
        }
    }
}

void LibraryView::delete_rom_event(lv_event_t *event)
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
            view->callbacks_.state_changed();
        }
    }
}

void LibraryView::destroy() noexcept
{
    if (library_panel_) lv_obj_delete(library_panel_);
    library_panel_ = nullptr;
    library_list_ = nullptr;
    library_toolbar_ = nullptr;
    refresh_button_ = nullptr;
    settings_button_ = nullptr;
    help_button_ = nullptr;
    rom_contexts_.clear();
}

void LibraryView::set_visible(bool visible)
{
    if (!library_panel_) return;
    if (visible) lv_obj_remove_flag(library_panel_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(library_panel_, LV_OBJ_FLAG_HIDDEN);
}

void LibraryView::apply_layout() noexcept
{
    if (!library_panel_) return;
    auto *parent = lv_obj_get_parent(library_panel_);
    const int width = lv_obj_get_content_width(parent);
    const bool landscape = width >= 800 && width > lv_obj_get_content_height(parent);
    lv_obj_set_style_pad_all(library_panel_, landscape ? 18 : 16, 0);
    lv_obj_set_style_pad_bottom(library_panel_, kBottomInset, 0);
}

void LibraryView::refresh_event(lv_event_t *event)
{
    auto *view = static_cast<LibraryView *>(lv_event_get_user_data(event));
    view->view_model_.refresh_library();
    view->rebuild_rom_list();
}

void LibraryView::settings_event(lv_event_t *event)
{
    auto *view = static_cast<LibraryView *>(lv_event_get_user_data(event));
    view->callbacks_.navigate(NesPage::settings);
}

void LibraryView::help_event(lv_event_t *event)
{
    auto *view = static_cast<LibraryView *>(lv_event_get_user_data(event));
    view->callbacks_.navigate(NesPage::help);
}

void LibraryView::add_rom_event(lv_event_t *event)
{
    auto *view = static_cast<LibraryView *>(lv_event_get_user_data(event));
    if (view->view_model_.state() == NesUiState::running ||
        view->view_model_.state() == NesUiState::stopping) return;
    view->callbacks_.navigate(NesPage::browser);
}
