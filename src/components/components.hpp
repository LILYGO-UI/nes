#ifndef LILYGO_UI_NES_COMPONENTS_HPP
#define LILYGO_UI_NES_COMPONENTS_HPP

#include <lvgl.h>

#include <cstdint>

namespace components {

inline constexpr std::uint32_t color_page_background      = 0xf2f2f7;
inline constexpr std::uint32_t color_container_background = 0xffffff;
inline constexpr std::uint32_t color_container_title      = 0x1c1c1e;
inline constexpr std::uint32_t color_text                 = 0x1c1c1e;
inline constexpr std::uint32_t color_secondary_text       = 0x8e8e93;
inline constexpr std::uint32_t color_border               = 0xe5e5ea;
inline constexpr std::uint32_t color_action               = 0x007aff;
inline constexpr std::uint32_t color_action_pressed       = 0x0062cc;
inline constexpr std::uint32_t color_tinted_surface       = 0xeaf3ff;
inline constexpr std::uint32_t color_grouped_surface      = 0xfafafd;
inline constexpr std::uint32_t color_destructive          = 0xff3b30;
inline constexpr std::uint32_t color_success              = 0x34c759;
inline constexpr std::uint32_t color_warning              = 0xffcc00;

lv_obj_t *create_page(lv_obj_t *parent);
lv_obj_t *create_header(lv_obj_t *parent, const char *title);
lv_obj_t *create_card(lv_obj_t *parent);
lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_event_cb_t callback, void *user_data);

}  // namespace components

#endif
