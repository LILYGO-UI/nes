#include "pages/settings/settings_view.hpp"

#include "components/components.hpp"
#include "components/page_helpers.hpp"

#include <cm0/typography.h>

using page_ui::control_size;
using page_ui::kBottomInset;
using page_ui::make_transparent;
using page_ui::set_font;

SettingsView::SettingsView(SettingsViewModel &view_model, const NesPageCallbacks &callbacks)
    : view_model_(view_model), callbacks_(callbacks)
{
}

lv_obj_t *SettingsView::create(lv_obj_t *parent)
{
    if (settings_panel_) return settings_panel_;
    settings_panel_ = lv_obj_create(parent);
    lv_obj_set_name_static(settings_panel_, "nes_settings_panel");
    lv_obj_set_size(settings_panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(settings_panel_, 0, 0);
    lv_obj_set_style_radius(settings_panel_, 0, 0);
    lv_obj_set_style_bg_color(settings_panel_, lv_color_hex(components::color_page_background), 0);
    lv_obj_set_style_bg_opa(settings_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(settings_panel_, 20, 0);
    lv_obj_set_style_pad_bottom(settings_panel_, kBottomInset, 0);
    lv_obj_set_style_pad_row(settings_panel_, 14, 0);
    lv_obj_clear_flag(settings_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(settings_panel_, LV_FLEX_FLOW_COLUMN);

    auto *heading = lv_obj_create(settings_panel_);
    make_transparent(heading);
    lv_obj_set_size(heading, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(heading, 12, 0);

    auto *title = lv_label_create(heading);
    lv_obj_set_name_static(title, "nes_settings_title");
    lv_label_set_text(title, "Key Mapping");
    set_font(title, 32);
    lv_obj_set_width(title, 0);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_color(title, lv_color_hex(components::color_container_title), 0);

    settings_back_ = components::create_button(heading, "Back", settings_event, this);
    lv_obj_set_name_static(settings_back_, "nes_settings_back");
    lv_obj_set_size(settings_back_, 104, 48);
    lv_obj_set_style_shadow_width(settings_back_, 0, 0);
    lv_obj_set_style_bg_color(settings_back_, lv_color_hex(components::color_tinted_surface), 0);
    lv_obj_set_style_bg_color(settings_back_, lv_color_hex(components::color_action), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(lv_obj_get_child(settings_back_, 0), lv_color_hex(components::color_action), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(settings_back_, 0), lv_color_hex(components::color_container_background),
                                LV_STATE_PRESSED);
    set_font(lv_obj_get_child(settings_back_, 0), 18);

    auto *description = lv_label_create(settings_panel_);
    lv_obj_set_name_static(description, "nes_settings_description");
    lv_label_set_text(description, "Player 1 keyboard controls — tap a row to change its key");
    lv_obj_set_width(description, LV_PCT(100));
    set_font(description, 16);
    lv_obj_set_style_text_color(description, lv_color_hex(components::color_secondary_text), 0);

    settings_status_ = lv_label_create(settings_panel_);
    lv_obj_set_name_static(settings_status_, "nes_settings_status");
    lv_obj_set_width(settings_status_, LV_PCT(100));
    lv_label_set_long_mode(settings_status_, LV_LABEL_LONG_WRAP);
    set_font(settings_status_, 15);
    lv_obj_set_style_text_color(settings_status_, lv_color_hex(components::color_secondary_text), 0);

    auto *table = lv_obj_create(settings_panel_);
    lv_obj_set_name_static(table, "nes_keymap_table");
    lv_obj_set_size(table, LV_PCT(100), 0);
    lv_obj_set_flex_grow(table, 1);
    lv_obj_set_style_border_width(table, 0, 0);
    lv_obj_set_style_border_color(table, lv_color_hex(components::color_border), 0);
    lv_obj_set_style_radius(table, 16, 0);
    lv_obj_set_style_bg_color(table, lv_color_hex(components::color_grouped_surface), 0);
    lv_obj_set_style_bg_opa(table, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(table, 12, 0);
    lv_obj_set_style_pad_row(table, control_size(2), 0);
    lv_obj_set_flex_flow(table, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(table, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(table, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(table, LV_SCROLLBAR_MODE_AUTO);

    auto add_header = [&]() {
        auto *row = lv_obj_create(table);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, control_size(36));
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(components::color_border), 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_hor(row, 4, 0);
        lv_obj_set_style_pad_ver(row, control_size(4), 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        auto *action_label = lv_label_create(row);
        lv_label_set_text(action_label, "Action");
        lv_obj_set_width(action_label, 0);
        lv_obj_set_flex_grow(action_label, 1);
        set_font(action_label, 14);
        lv_obj_set_style_text_color(action_label, lv_color_hex(components::color_secondary_text), 0);
        auto *key_label = lv_label_create(row);
        lv_label_set_text(key_label, "P1");
        lv_obj_set_width(key_label, 72);
        lv_obj_set_style_text_align(key_label, LV_TEXT_ALIGN_CENTER, 0);
        set_font(key_label, 14);
        lv_obj_set_style_text_color(key_label, lv_color_hex(components::color_secondary_text), 0);
    };

    add_header();
    static constexpr const char *const row_names[nes_mapping_action_count] = {
        "nes_keymap_up",    "nes_keymap_down",  "nes_keymap_left", "nes_keymap_right", "nes_keymap_a",
        "nes_keymap_b",     "nes_keymap_rapid_a", "nes_keymap_rapid_b", "nes_keymap_start", "nes_keymap_select",
    };
    static constexpr const char *const value_names[nes_mapping_action_count] = {
        "nes_keymap_value_up",    "nes_keymap_value_down",  "nes_keymap_value_left",
        "nes_keymap_value_right", "nes_keymap_value_a",      "nes_keymap_value_b",
        "nes_keymap_value_rapid_a", "nes_keymap_value_rapid_b", "nes_keymap_value_start",
        "nes_keymap_value_select",
    };
    for (std::size_t index = 0; index < nes_mapping_action_count; ++index) {
        const auto action = static_cast<NesMappingAction>(index);
        mapping_contexts_[index] = {this, action};
        auto *row = lv_button_create(table);
        lv_obj_set_name_static(row, row_names[index]);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, control_size(46));
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(components::color_border), 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(components::color_tinted_surface), LV_STATE_PRESSED);
        lv_obj_set_style_pad_hor(row, 4, 0);
        lv_obj_set_style_pad_ver(row, control_size(4), 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, mapping_event, LV_EVENT_CLICKED, &mapping_contexts_[index]);

        auto *action_label = lv_label_create(row);
        lv_label_set_text(action_label, nes_mapping_action_label(action));
        lv_obj_set_width(action_label, 0);
        lv_obj_set_flex_grow(action_label, 1);
        set_font(action_label, 18);
        lv_obj_set_style_text_color(action_label, lv_color_hex(components::color_text), 0);

        auto *key_label = lv_label_create(row);
        mapping_key_labels_[index] = key_label;
        lv_obj_set_name_static(key_label, value_names[index]);
        lv_obj_set_width(key_label, 96);
        lv_obj_set_style_text_align(key_label, LV_TEXT_ALIGN_CENTER, 0);
        set_font(key_label, 20);
        lv_obj_set_style_text_color(key_label, lv_color_hex(components::color_action), 0);
    }
    refresh_mapping_labels();

    auto *unsupported = lv_label_create(settings_panel_);
    lv_obj_set_name_static(unsupported, "nes_settings_p2_note");
    lv_label_set_text(unsupported, "P2 controls are not supported.");
    lv_obj_set_width(unsupported, LV_PCT(100));
    lv_label_set_long_mode(unsupported, LV_LABEL_LONG_WRAP);
    set_font(unsupported, 14);
    lv_obj_set_style_text_color(unsupported, lv_color_hex(components::color_secondary_text), 0);
    refresh_status();
    return settings_panel_;
}

void SettingsView::destroy() noexcept
{
    view_model_.cancel_capture();
    if (settings_panel_) lv_obj_delete(settings_panel_);
    settings_panel_ = nullptr;
    settings_back_ = nullptr;
    settings_status_ = nullptr;
    mapping_key_labels_.fill(nullptr);
}

void SettingsView::set_visible(bool visible)
{
    view_model_.cancel_capture();
    if (!settings_panel_) return;
    if (visible) {
        refresh_mapping_labels();
        refresh_status();
        lv_obj_clear_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
    }
}

void SettingsView::refresh_mapping_labels()
{
    const auto &map = view_model_.keymap();
    for (std::size_t index = 0; index < nes_mapping_action_count; ++index) {
        if (!mapping_key_labels_[index]) continue;
        const auto action = static_cast<NesMappingAction>(index);
        const auto label  = nes_key_label(map.key_for(action));
        lv_label_set_text(mapping_key_labels_[index], label.c_str());
    }
}

void SettingsView::refresh_status()
{
    if (!settings_status_) return;
    std::uint32_t color = components::color_secondary_text;
    switch (view_model_.feedback()) {
    case SettingsFeedback::neutral: break;
    case SettingsFeedback::action: color = components::color_action; break;
    case SettingsFeedback::error: color = components::color_destructive; break;
    case SettingsFeedback::success: color = components::color_success; break;
    case SettingsFeedback::warning: color = components::color_warning; break;
    }
    lv_label_set_text(settings_status_, view_model_.status().c_str());
    lv_obj_set_style_text_color(settings_status_, lv_color_hex(color), 0);
}

void SettingsView::capture_mapping_key(std::uint32_t key)
{
    view_model_.capture_mapping_key(key);
    refresh_mapping_labels();
    refresh_status();
}

bool SettingsView::capturing() const noexcept
{
    return view_model_.mapping_capture().has_value();
}

void SettingsView::settings_event(lv_event_t *event)
{
    auto *view = static_cast<SettingsView *>(lv_event_get_user_data(event));
    if (view && view->callbacks_.navigate) view->callbacks_.navigate(NesPage::library);
}

void SettingsView::mapping_event(lv_event_t *event)
{
    auto *context = static_cast<MappingContext *>(lv_event_get_user_data(event));
    if (!context || !context->view) return;
    context->view->view_model_.begin_mapping_capture(context->action);
    context->view->refresh_status();
}
