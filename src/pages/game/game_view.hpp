#ifndef LILYGO_UI_NES_PAGES_GAME_GAME_VIEW_HPP
#define LILYGO_UI_NES_PAGES_GAME_GAME_VIEW_HPP

#include "pages/game/game_view_model.hpp"
#include "pages/nes/frame_scaler.hpp"
#include "pages/nes/page_navigation.hpp"
#include "platform/multitouch_reader.hpp"
#include "platform/nes_gpu_frame.hpp"

#include <lvgl.h>

#include <array>
#include <cstdint>

// Owns the game page, frame resources, and gameplay input. The application
// shell owns the keypad subscription and calls tick() from its UI timer.
class GameView {
public:
    GameView(GameViewModel &view_model, const NesPageCallbacks &callbacks);

    lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;
    void set_visible(bool visible);
    [[nodiscard]] lv_obj_t *root() const noexcept { return game_panel_; }
    void update_status();
    void apply_layout() noexcept;
    void tick();
    bool can_start();
    void keyboard_input(std::uint32_t key, bool pressed);
    void clear_keyboard() noexcept;
    void set_input_blocked(bool blocked);

private:
    struct ControlContext {
        GameView *view = nullptr;
        NesButton button = NesButton::up;
    };

    static void stop_event(lv_event_t *event);
    static void state_event(lv_event_t *event);
    static void control_event(lv_event_t *event);

    [[nodiscard]] bool accepts_input() const noexcept;
    void clear_controls() noexcept;
    void sync_controls() noexcept;
    void update_multitouch();
    void update_host_keyboard() noexcept;
    void resize_frame(int width, int height);
    lv_obj_t *create_control_button(lv_obj_t *parent, const char *text, NesButton button,
                                    std::uint32_t color, std::uint32_t text_color = 0xffffff);

    GameViewModel &view_model_;
    NesPageCallbacks callbacks_;
    MultitouchReader multitouch_;
    std::array<std::uint16_t, NesEmulator::frame_pixels> frame_buffer_{};
#if LV_USE_DRAW_OPENGLES
    NesGpuFrame gpu_frame_;
    bool gpu_error_reported_ = false;
#else
    NesFrameScaler scaled_frame_;
    lv_image_dsc_t frame_descriptor_{};
#endif
    std::array<ControlContext, 8> control_contexts_{};
    std::array<int32_t, 4> dpad_tracks_{};
    lv_obj_t *parent_ = nullptr;
    lv_obj_t *game_panel_ = nullptr;
    lv_obj_t *state_toolbar_ = nullptr;
    lv_obj_t *save_state_button_ = nullptr;
    lv_obj_t *load_state_button_ = nullptr;
    lv_obj_t *state_status_ = nullptr;
    lv_obj_t *game_content_ = nullptr;
    lv_obj_t *game_display_ = nullptr;
    lv_obj_t *frame_panel_ = nullptr;
    lv_obj_t *frame_image_ = nullptr;
    lv_obj_t *frame_placeholder_ = nullptr;
    lv_obj_t *system_controls_ = nullptr;
    lv_obj_t *stop_button_ = nullptr;
    lv_obj_t *dpad_ = nullptr;
    lv_obj_t *action_buttons_ = nullptr;
    std::array<lv_obj_t *, 8> control_buttons_{};
    std::uint32_t lvgl_pressed_mask_ = 0;
    std::uint32_t multitouch_pressed_mask_ = 0;
    std::uint32_t keyboard_pressed_mask_ = 0;
    std::uint32_t keyboard_rapid_mask_ = 0;
    std::uint32_t sent_pressed_mask_ = 0;
    std::uint32_t sent_rapid_mask_ = 0;
    bool input_blocked_ = false;
};

#endif
