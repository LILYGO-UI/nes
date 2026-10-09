#ifndef LILYGO_UI_NES_PAGE_HELPERS_HPP
#define LILYGO_UI_NES_PAGE_HELPERS_HPP

#include <cm0/typography.h>
#include <lvgl.h>

namespace page_ui {

#if LV_USE_SYSMON && (LV_USE_PERF_MONITOR || LV_USE_MEM_MONITOR)
inline constexpr int kBottomInset = 44;
#else
inline constexpr int kBottomInset = 20;
#endif

constexpr int control_size(int original, float scale = 1.6F)
{
    return static_cast<int>(original * scale + 0.5F);
}

inline void set_font(lv_obj_t *object, int size)
{
    if (auto *font = lilygo_ui_font_get(size)) lv_obj_set_style_text_font(object, font, 0);
}

inline void make_transparent(lv_obj_t *object)
{
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

}  // namespace page_ui

#endif
