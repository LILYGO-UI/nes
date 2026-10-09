#include "pages/rom_browser/rom_browser_view.hpp"

#include "components/components.hpp"
#include "components/page_helpers.hpp"

#include <cm0/typography.h>

using page_ui::kBottomInset;
using page_ui::make_transparent;
using page_ui::set_font;

RomBrowserView::RomBrowserView(RomBrowserViewModel &view_model, const NesPageCallbacks &callbacks)
    : view_model_(view_model), callbacks_(callbacks)
{
}

lv_obj_t *RomBrowserView::create(lv_obj_t *parent)
{
    if (browser_panel_) return browser_panel_;
    browser_panel_ = lv_obj_create(parent);
    lv_obj_set_name_static(browser_panel_, "nes_rom_browser");
    lv_obj_set_size(browser_panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(browser_panel_, 0, 0);
    lv_obj_set_style_radius(browser_panel_, 0, 0);
    lv_obj_set_style_bg_color(browser_panel_, lv_color_hex(components::color_page_background), 0);
    lv_obj_set_style_bg_opa(browser_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(browser_panel_, 20, 0);
    lv_obj_set_style_pad_bottom(browser_panel_, kBottomInset, 0);
    lv_obj_set_style_pad_row(browser_panel_, 14, 0);
    lv_obj_clear_flag(browser_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(browser_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(browser_panel_, LV_FLEX_FLOW_COLUMN);

    auto *heading = lv_obj_create(browser_panel_);
    make_transparent(heading);
    lv_obj_set_size(heading, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto *title = lv_label_create(heading);
    lv_label_set_text(title, "Add ROM");
    set_font(title, 32);
    lv_obj_set_width(title, 0);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_color(title, lv_color_hex(components::color_container_title), 0);

    browser_cancel_ = components::create_button(heading, "Cancel", browser_navigation_event, this);
    lv_obj_set_name_static(browser_cancel_, "nes_browser_cancel");
    lv_obj_set_size(browser_cancel_, 104, 48);
    lv_obj_set_style_shadow_width(browser_cancel_, 0, 0);
    lv_obj_set_style_bg_color(browser_cancel_, lv_color_hex(components::color_tinted_surface), 0);
    lv_obj_set_style_bg_color(browser_cancel_, lv_color_hex(components::color_action), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(lv_obj_get_child(browser_cancel_, 0), lv_color_hex(components::color_action), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(browser_cancel_, 0), lv_color_hex(components::color_container_background),
                                LV_STATE_PRESSED);

    auto *navigation = lv_obj_create(browser_panel_);
    make_transparent(navigation);
    lv_obj_set_size(navigation, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(navigation, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(navigation, 10, 0);
    const auto navigation_button = [&](const char *text, const char *name) {
        auto *button = components::create_button(navigation, text, browser_navigation_event, this);
        lv_obj_set_name_static(button, name);
        lv_obj_set_size(button, 0, 48);
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(components::color_tinted_surface), 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(components::color_action), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(lv_obj_get_child(button, 0), lv_color_hex(components::color_action), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(button, 0), lv_color_hex(components::color_container_background),
                                    LV_STATE_PRESSED);
        set_font(lv_obj_get_child(button, 0), 18);
        return button;
    };
    browser_home_ = navigation_button(LV_SYMBOL_HOME " Home", "nes_browser_home");
    browser_root_ = navigation_button("/ Root", "nes_browser_root");
    browser_up_   = navigation_button(LV_SYMBOL_UP " Up", "nes_browser_up");

    // Keep long filesystem paths readable without consuming the file list.
    auto *path_container = lv_obj_create(browser_panel_);
    make_transparent(path_container);
    lv_obj_set_size(path_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(path_container, 84, 0);
    lv_obj_add_flag(path_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(path_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(path_container, LV_SCROLLBAR_MODE_AUTO);
    browser_path_ = lv_label_create(path_container);
    lv_obj_set_name_static(browser_path_, "nes_browser_path");
    lv_obj_set_width(browser_path_, LV_PCT(100));
    lv_label_set_long_mode(browser_path_, LV_LABEL_LONG_WRAP);
    set_font(browser_path_, 16);
    lv_obj_set_style_text_color(browser_path_, lv_color_hex(components::color_text), 0);
    lv_obj_set_style_bg_color(path_container, lv_color_hex(components::color_grouped_surface), 0);
    lv_obj_set_style_bg_opa(path_container, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(path_container, 12, 0);
    lv_obj_set_style_pad_all(path_container, 12, 0);

    browser_status_ = lv_label_create(browser_panel_);
    lv_obj_set_name_static(browser_status_, "nes_browser_status");
    lv_obj_set_width(browser_status_, LV_PCT(100));
    lv_label_set_long_mode(browser_status_, LV_LABEL_LONG_WRAP);
    set_font(browser_status_, 16);

    browser_list_ = lv_obj_create(browser_panel_);
    lv_obj_set_name_static(browser_list_, "nes_browser_list");
    make_transparent(browser_list_);
    lv_obj_set_size(browser_list_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(browser_list_, 1);
    lv_obj_set_flex_flow(browser_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(browser_list_, 8, 0);
    lv_obj_set_style_pad_right(browser_list_, 6, 0);
    lv_obj_add_flag(browser_list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(browser_list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(browser_list_, LV_SCROLLBAR_MODE_AUTO);
    return browser_panel_;
}

void RomBrowserView::destroy() noexcept
{
    if (browser_panel_) lv_obj_delete(browser_panel_);
    browser_panel_ = nullptr;
    browser_path_ = nullptr;
    browser_status_ = nullptr;
    browser_list_ = nullptr;
    browser_home_ = nullptr;
    browser_root_ = nullptr;
    browser_up_ = nullptr;
    browser_cancel_ = nullptr;
    browser_contexts_.clear();
}

void RomBrowserView::set_visible(bool visible)
{
    if (!browser_panel_) return;
    if (visible) lv_obj_clear_flag(browser_panel_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(browser_panel_, LV_OBJ_FLAG_HIDDEN);
}

void RomBrowserView::rebuild_browser_list()
{
    if (!browser_panel_) return;
    lv_label_set_text(browser_path_, view_model_.browser_path().c_str());
    lv_obj_scroll_to_y(lv_obj_get_parent(browser_path_), 0, LV_ANIM_OFF);
    const auto &status = view_model_.browser_status();
    lv_label_set_text(browser_status_, status.empty() ? "Select a .nes file to copy it to $HOME/nes/rom" : status.c_str());
    lv_obj_set_style_text_color(browser_status_, lv_color_hex(status.empty() ? components::color_secondary_text : components::color_action), 0);
    if (view_model_.browser_path().empty() || view_model_.browser_path() == "/") {
        lv_obj_add_state(browser_up_, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(browser_up_, LV_STATE_DISABLED);
    }

    lv_obj_clean(browser_list_);
    browser_contexts_.clear();
    const auto &entries = view_model_.browser_entries();
    browser_contexts_.resize(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto &entry = entries[index];
        browser_contexts_[index] = {this, index};
        auto *row = lv_button_create(browser_list_);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(row, 56, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(components::color_border), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(components::color_container_background), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(components::color_tinted_surface), LV_STATE_PRESSED);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 12, 0);
        lv_obj_set_style_pad_column(row, 12, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, browser_entry_event, LV_EVENT_CLICKED, &browser_contexts_[index]);

        auto *name = lv_label_create(row);
        lv_label_set_text(name, entry.name.c_str());
        lv_obj_set_width(name, 0);
        lv_obj_set_flex_grow(name, 1);
        lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
        set_font(name, 18);
        lv_obj_set_style_text_color(name, lv_color_hex(components::color_text), 0);

        auto *icon = lv_label_create(row);
        lv_label_set_text(icon, entry.directory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_PLUS);
        set_font(icon, 22);
        lv_obj_set_style_text_color(icon, lv_color_hex(entry.directory ? components::color_secondary_text : components::color_action), 0);
    }
    if (entries.empty()) {
        auto *empty = lv_label_create(browser_list_);
        lv_label_set_text(empty, "No folders or .nes files in this directory");
        lv_obj_set_width(empty, LV_PCT(100));
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        set_font(empty, 18);
        lv_obj_set_style_text_color(empty, lv_color_hex(components::color_secondary_text), 0);
    }
    lv_obj_scroll_to_y(browser_list_, 0, LV_ANIM_OFF);
}

void RomBrowserView::browser_navigation_event(lv_event_t *event)
{
    auto *view = static_cast<RomBrowserView *>(lv_event_get_user_data(event));
    auto *button = lv_event_get_target_obj(event);
    if (button == view->browser_cancel_) {
        if (view->callbacks_.navigate) view->callbacks_.navigate(NesPage::library);
        return;
    }
    if (button == view->browser_home_) {
        view->view_model_.open_browser();
    } else if (button == view->browser_root_) {
        view->view_model_.browse_directory("/");
    } else if (button == view->browser_up_) {
        view->view_model_.browse_parent();
    }
    view->rebuild_browser_list();
}

void RomBrowserView::browser_entry_event(lv_event_t *event)
{
    const auto *context = static_cast<BrowserContext *>(lv_event_get_user_data(event));
    if (!context || !context->view) return;
    auto *view = context->view;
    const auto &entries = view->view_model_.browser_entries();
    if (context->index >= entries.size()) return;
    // Navigation replaces both the model entries and the callback contexts.
    const auto entry = entries[context->index];
    if (entry.directory) {
        view->view_model_.browse_directory(entry.path);
        view->rebuild_browser_list();
    } else if (view->view_model_.import_rom(entry.path)) {
        if (view->callbacks_.library_changed) view->callbacks_.library_changed();
        if (view->callbacks_.state_changed) view->callbacks_.state_changed();
        if (view->callbacks_.navigate) view->callbacks_.navigate(NesPage::library);
    } else {
        if (view->callbacks_.state_changed) view->callbacks_.state_changed();
    }
}
