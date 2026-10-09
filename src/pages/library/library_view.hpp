#ifndef LILYGO_UI_NES_LIBRARY_VIEW_HPP
#define LILYGO_UI_NES_LIBRARY_VIEW_HPP

#include "pages/library/library_view_model.hpp"
#include "pages/nes/page_navigation.hpp"

#include <lvgl.h>
#include <vector>

class LibraryView {
public:
    LibraryView(LibraryViewModel &view_model, const NesPageCallbacks &callbacks) noexcept;
    lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;
    void set_visible(bool visible);
    void rebuild_rom_list();
    void apply_layout() noexcept;
    [[nodiscard]] lv_obj_t *root() const noexcept { return library_panel_; }

private:
    struct RomContext {
        LibraryView *view = nullptr;
        std::size_t index = 0;
        lv_obj_t *row = nullptr;
        lv_obj_t *remove = nullptr;
        lv_point_t press_point{};
        int32_t initial_offset = 0;
        bool tracking = false;
        bool moved = false;
        bool horizontal = false;
    };
    static void refresh_event(lv_event_t *event);
    static void settings_event(lv_event_t *event);
    static void help_event(lv_event_t *event);
    static void add_rom_event(lv_event_t *event);
    static void rom_event(lv_event_t *event);
    static void delete_rom_event(lv_event_t *event);
    void set_rom_offset(RomContext &context, int32_t offset);
    lv_obj_t *create_icon_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t callback);

    LibraryViewModel &view_model_;
    const NesPageCallbacks &callbacks_;
    std::vector<RomContext> rom_contexts_;
    lv_obj_t *library_panel_ = nullptr;
    lv_obj_t *library_list_ = nullptr;
    lv_obj_t *library_toolbar_ = nullptr;
    lv_obj_t *refresh_button_ = nullptr;
    lv_obj_t *settings_button_ = nullptr;
    lv_obj_t *help_button_ = nullptr;
};

#endif
