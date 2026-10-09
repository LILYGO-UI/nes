#include "pages/help/help_view.hpp"

#include "components/components.hpp"
#include "components/page_helpers.hpp"

#include <cm0/typography.h>

using page_ui::kBottomInset;
using page_ui::make_transparent;
using page_ui::set_font;

HelpView::HelpView(HelpViewModel &view_model, const NesPageCallbacks &callbacks)
    : view_model_(view_model), callbacks_(callbacks)
{
}

lv_obj_t *HelpView::create(lv_obj_t *parent)
{
    if (help_panel_) return help_panel_;
    help_panel_ = lv_obj_create(parent);
    lv_obj_set_name_static(help_panel_, "nes_help_panel");
    lv_obj_set_size(help_panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(help_panel_, 0, 0);
    lv_obj_set_style_radius(help_panel_, 0, 0);
    lv_obj_set_style_bg_color(help_panel_, lv_color_hex(components::color_page_background), 0);
    lv_obj_set_style_bg_opa(help_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(help_panel_, 20, 0);
    lv_obj_set_style_pad_bottom(help_panel_, kBottomInset, 0);
    lv_obj_set_style_pad_row(help_panel_, 16, 0);
    lv_obj_set_flex_flow(help_panel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(help_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(help_panel_, LV_OBJ_FLAG_HIDDEN);

    auto *heading = lv_obj_create(help_panel_);
    make_transparent(heading);
    lv_obj_set_size(heading, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto *title = lv_label_create(heading);
    lv_label_set_text(title, "Help");
    set_font(title, 32);
    lv_obj_set_width(title, 0);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_color(title, lv_color_hex(components::color_container_title), 0);

    help_back_ = components::create_button(heading, "Back", help_event, this);
    lv_obj_set_name_static(help_back_, "nes_help_back");
    lv_obj_set_size(help_back_, 104, 48);
    set_font(lv_obj_get_child(help_back_, 0), 18);

    auto *content = lv_obj_create(help_panel_);
    lv_obj_set_name_static(content, "nes_help_content");
    make_transparent(content);
    lv_obj_set_size(content, LV_PCT(100), 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 12, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);

    const auto section = [&](const char *heading_text) {
        auto *card = components::create_card(content);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 8, 0);
        auto *label = lv_label_create(card);
        lv_label_set_text(label, heading_text);
        lv_obj_set_width(label, LV_PCT(100));
        set_font(label, 22);
        lv_obj_set_style_text_color(label, lv_color_hex(components::color_container_title), 0);
        return card;
    };
    const auto paragraph = [](lv_obj_t *card, const char *text) {
        auto *label = lv_label_create(card);
        lv_label_set_text(label, text);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        set_font(label, 16);
        lv_obj_set_style_text_color(label, lv_color_hex(components::color_text), 0);
        lv_obj_set_style_text_line_space(label, 3, 0);
        return label;
    };

    auto *location = section("ROM location");
    const auto &directory = view_model_.rom_directory();
    auto *path = paragraph(location, directory.empty() ? "ROM folder unavailable" : directory.c_str());
    lv_obj_set_name_static(path, "nes_help_path");
    lv_obj_set_style_text_color(path, lv_color_hex(components::color_action), 0);
    paragraph(location, directory.empty()
                            ? "Set a valid home directory before adding ROMs."
                            : "Keep uncompressed .nes files in this folder. After copying files here, tap Refresh "
                              "below the game list to see them.");

    paragraph(section("Add & play"),
              "Tap Add ROM at the end of the list and choose a .nes file. The app copies it into the ROM folder and "
              "keeps the original file. Existing files with the same name are not overwritten.\n\n"
              "Tap a game in the list to start playing.");
    paragraph(section("Delete a ROM"),
              "Swipe left on a game to reveal the trash button, then tap it to delete the ROM file from "
              "the folder. Swipe right to close the action without deleting. Scroll vertically to browse games.");
    paragraph(section("Game controls"),
              "Use the direction pad and A/B buttons together. Start and Select work like the original "
              "controller. Tap Stop to close the game and return to the list.\n\n"
              "Open Settings below the game list to view or change Player 1 keyboard controls. Tap a control, "
              "then press its new key. Changes are saved automatically.");

    auto *states = section("Saved states");
    paragraph(states, "While playing, tap Save beside Select and Start to save your current progress. Each game has one "
                      "saved state; saving again replaces it. Tap Load to return to that point, including after "
                      "closing and reopening the game. Wait for the status message before closing.\n\n"
                      "Games with battery-backed saves also keep their cartridge data automatically.");
    const auto &states_directory = view_model_.states_directory();
    auto *states_path = paragraph(states, states_directory.empty() ? "Save folder unavailable" : states_directory.c_str());
    lv_obj_set_name_static(states_path, "nes_help_states_path");
    lv_obj_set_style_text_color(states_path, lv_color_hex(components::color_action), 0);
    return help_panel_;
}

void HelpView::destroy() noexcept
{
    if (help_panel_) lv_obj_delete(help_panel_);
    help_panel_ = nullptr;
    help_back_ = nullptr;
}

void HelpView::set_visible(bool visible)
{
    if (!help_panel_) return;
    if (visible) lv_obj_clear_flag(help_panel_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(help_panel_, LV_OBJ_FLAG_HIDDEN);
}

void HelpView::help_event(lv_event_t *event)
{
    auto *view = static_cast<HelpView *>(lv_event_get_user_data(event));
    if (view && view->callbacks_.navigate) view->callbacks_.navigate(NesPage::library);
}
