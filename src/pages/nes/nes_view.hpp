#ifndef LILYGO_UI_NES_PAGES_NES_NES_VIEW_HPP
#define LILYGO_UI_NES_PAGES_NES_NES_VIEW_HPP

#include "pages/nes/frame_scaler.hpp"
#include "pages/nes/nes_view_model.hpp"
#include "platform/multitouch_reader.hpp"
#include "platform/nes_gpu_frame.hpp"

#include <lvgl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class NesView {
public:
    explicit NesView(NesViewModel &view_model) noexcept;

    lv_obj_t *create(lv_obj_t *parent, const char *application_name);
    void destroy() noexcept;

private:
    struct ControlContext {
        NesView *view    = nullptr;
        NesButton button = NesButton::up;
    };

    struct RomContext {
        NesView *view     = nullptr;
        std::size_t index = 0;
        lv_obj_t *row     = nullptr;
        lv_obj_t *remove  = nullptr;
        lv_point_t press_point{};
        int32_t initial_offset = 0;
        bool tracking          = false;
        bool moved             = false;
        bool horizontal        = false;
    };

    static void size_changed_event(lv_event_t *event);
    static void refresh_event(lv_event_t *event);
    static void rom_event(lv_event_t *event);
    static void delete_rom_event(lv_event_t *event);
    static void stop_event(lv_event_t *event);
    static void control_event(lv_event_t *event);
    static void timer_event(lv_timer_t *timer);

    void create_library();
    void create_game();
    void rebuild_rom_list();
    void set_rom_offset(RomContext &context, int32_t offset);
    void show_game(bool visible);
    void update_status();
    void update_multitouch();
    void sync_controls() noexcept;
    void apply_layout() noexcept;
    void resize_frame(int width, int height);

    lv_obj_t *create_icon_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t callback);
    lv_obj_t *create_control_button(lv_obj_t *parent, const char *text, NesButton button, std::uint32_t color,
                                    std::uint32_t text_color = 0xffffff);

    NesViewModel &view_model_;
    MultitouchReader multitouch_;
    std::array<std::uint16_t, NesEmulator::frame_pixels> frame_buffer_{};
#if LV_USE_DRAW_OPENGLES
    NesGpuFrame gpu_frame_;
#else
    NesFrameScaler scaled_frame_;
    lv_image_dsc_t frame_descriptor_{};
#endif
    std::array<ControlContext, 8> control_contexts_{};
    std::vector<RomContext> rom_contexts_;

    lv_obj_t *surface_           = nullptr;
    lv_obj_t *header_            = nullptr;
    lv_obj_t *refresh_button_    = nullptr;
    lv_obj_t *body_              = nullptr;
    lv_obj_t *library_panel_     = nullptr;
    lv_obj_t *library_summary_   = nullptr;
    lv_obj_t *library_list_      = nullptr;
    lv_obj_t *directory_label_   = nullptr;
    lv_obj_t *status_label_      = nullptr;
    lv_obj_t *game_panel_        = nullptr;
    lv_obj_t *game_content_      = nullptr;
    lv_obj_t *game_display_      = nullptr;
    lv_obj_t *frame_panel_       = nullptr;
    lv_obj_t *frame_image_       = nullptr;
    lv_obj_t *frame_placeholder_ = nullptr;
    lv_obj_t *game_status_label_ = nullptr;
    lv_obj_t *system_controls_   = nullptr;
    lv_obj_t *dpad_              = nullptr;
    lv_obj_t *action_buttons_    = nullptr;
    std::array<lv_obj_t *, 8> control_buttons_{};
    lv_timer_t *timer_                     = nullptr;
    std::uint32_t lvgl_pressed_mask_       = 0;
    std::uint32_t multitouch_pressed_mask_ = 0;
    std::uint32_t sent_pressed_mask_       = 0;
};

#endif
