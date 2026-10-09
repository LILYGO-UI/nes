#ifndef LILYGO_UI_NES_PAGES_ROM_BROWSER_ROM_BROWSER_VIEW_HPP
#define LILYGO_UI_NES_PAGES_ROM_BROWSER_ROM_BROWSER_VIEW_HPP

#include "pages/nes/page_navigation.hpp"
#include "pages/rom_browser/rom_browser_view_model.hpp"

#include <lvgl.h>

#include <cstddef>
#include <vector>

class RomBrowserView {
public:
    RomBrowserView(RomBrowserViewModel &view_model, const NesPageCallbacks &callbacks);

    lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;
    void set_visible(bool visible);
    [[nodiscard]] lv_obj_t *root() const noexcept { return browser_panel_; }
    void rebuild_browser_list();

private:
    struct BrowserContext {
        RomBrowserView *view = nullptr;
        std::size_t index = 0;
    };

    static void browser_navigation_event(lv_event_t *event);
    static void browser_entry_event(lv_event_t *event);

    RomBrowserViewModel &view_model_;
    NesPageCallbacks callbacks_;
    std::vector<BrowserContext> browser_contexts_;
    lv_obj_t *browser_panel_ = nullptr;
    lv_obj_t *browser_path_ = nullptr;
    lv_obj_t *browser_status_ = nullptr;
    lv_obj_t *browser_list_ = nullptr;
    lv_obj_t *browser_home_ = nullptr;
    lv_obj_t *browser_root_ = nullptr;
    lv_obj_t *browser_up_ = nullptr;
    lv_obj_t *browser_cancel_ = nullptr;
};

#endif
