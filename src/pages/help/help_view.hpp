#ifndef LILYGO_UI_NES_PAGES_HELP_HELP_VIEW_HPP
#define LILYGO_UI_NES_PAGES_HELP_HELP_VIEW_HPP

#include "pages/help/help_view_model.hpp"
#include "pages/nes/page_navigation.hpp"

#include <lvgl.h>

class HelpView {
public:
    HelpView(HelpViewModel &view_model, const NesPageCallbacks &callbacks);

    lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;
    void set_visible(bool visible);
    [[nodiscard]] lv_obj_t *root() const noexcept { return help_panel_; }

private:
    static void help_event(lv_event_t *event);

    HelpViewModel &view_model_;
    NesPageCallbacks callbacks_;
    lv_obj_t *help_panel_ = nullptr;
    lv_obj_t *help_back_ = nullptr;
};

#endif
