#include "pages/game/game_view.hpp"

#include "components/components.hpp"
#include "components/page_helpers.hpp"
#include "emulator/nes_input_mapping.hpp"
#include "emulator/nes_performance.hpp"

#include <cm0/typography.h>

#include <algorithm>

#if defined(CM0_APP_SIMULATOR)
#include <SDL.h>
#endif

namespace {

using page_ui::control_size;
using page_ui::make_transparent;
using page_ui::set_font;

constexpr std::uint32_t kColorGameBackground = 0x151515;
constexpr std::uint32_t kColorScreenBorder = 0x636366;
constexpr std::uint32_t kColorMutedText = components::color_secondary_text;
constexpr std::uint32_t kColorAccent = components::color_action;
constexpr std::uint32_t kColorDestructive = components::color_destructive;
constexpr std::uint32_t kColorSuccess = components::color_success;
constexpr std::uint32_t kColorWarning = components::color_warning;
constexpr std::uint32_t kColorControl = 0x2c2c2e;
constexpr std::uint32_t kColorControlPressed = 0x48484a;
constexpr float kControlScale = 1.6F;
constexpr float kSystemControlScale = 0.9F;
constexpr int kStateToolbarHeight = 28;
constexpr int kStateButtonWidth = 84;
constexpr int kSystemButtonGap = 8;

bool point_inside(const TouchPoint &point, lv_obj_t *object)
{
    if (!point.active || !object || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return false;
    lv_area_t area{};
    lv_obj_get_coords(object, &area);
    return point.x >= area.x1 && point.x <= area.x2 && point.y >= area.y1 && point.y <= area.y2;
}

}  // namespace

GameView::GameView(GameViewModel &view_model, const NesPageCallbacks &callbacks)
    : view_model_(view_model), callbacks_(callbacks)
{
}

void GameView::destroy() noexcept
{
    input_blocked_ = true;
    multitouch_.stop();
    view_model_.stop_rom();
    clear_controls();
    if (game_panel_) lv_obj_delete(game_panel_);
#if LV_USE_DRAW_OPENGLES
    gpu_frame_.reset();
    gpu_error_reported_ = false;
#else
    scaled_frame_ = {};
    frame_descriptor_ = {};
#endif
    parent_ = nullptr;
    game_panel_ = nullptr;
    state_toolbar_ = nullptr;
    save_state_button_ = nullptr;
    load_state_button_ = nullptr;
    state_status_ = nullptr;
    game_content_ = nullptr;
    game_display_ = nullptr;
    frame_panel_ = nullptr;
    frame_image_ = nullptr;
    frame_placeholder_ = nullptr;
    system_controls_ = nullptr;
    stop_button_ = nullptr;
    dpad_ = nullptr;
    action_buttons_ = nullptr;
    control_buttons_.fill(nullptr);
    control_contexts_.fill({});
    input_blocked_ = false;
}

bool GameView::accepts_input() const noexcept
{
    return game_panel_ && !input_blocked_ && !lv_obj_has_flag(game_panel_, LV_OBJ_FLAG_HIDDEN);
}

void GameView::clear_controls() noexcept
{
    lvgl_pressed_mask_ = 0;
    multitouch_pressed_mask_ = 0;
    keyboard_pressed_mask_ = 0;
    keyboard_rapid_mask_ = 0;
    sync_controls();
    for (auto *button : control_buttons_) {
        if (button) lv_obj_remove_state(button, LV_STATE_PRESSED);
    }
}

void GameView::clear_keyboard() noexcept
{
    keyboard_pressed_mask_ = 0;
    keyboard_rapid_mask_ = 0;
    sync_controls();
}

void GameView::set_input_blocked(bool blocked)
{
    input_blocked_ = blocked;
    if (blocked) clear_controls();
}

void GameView::set_visible(bool visible)
{
    if (!game_panel_) return;
    if (visible) {
        lv_obj_clear_flag(game_panel_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(game_panel_, LV_OBJ_FLAG_HIDDEN);
        clear_controls();
        lv_obj_add_flag(frame_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(frame_placeholder_, LV_OBJ_FLAG_HIDDEN);
    }
    update_status();
}

bool GameView::can_start()
{
    if (!game_panel_) return false;
#if LV_USE_DRAW_OPENGLES
    if (!gpu_frame_.ready()) {
        gpu_error_reported_ = true;
        if (callbacks_.show_error) callbacks_.show_error("GPU unavailable; see application log");
        return false;
    }
#endif
    return true;
}

void GameView::update_status()
{
    if (!state_status_) return;
    const bool pending = view_model_.state_operation_pending();
    const bool enabled = view_model_.state() == NesUiState::running && !pending;
    for (auto *button : {save_state_button_, load_state_button_}) {
        if (enabled) lv_obj_remove_state(button, LV_STATE_DISABLED);
        else lv_obj_add_state(button, LV_STATE_DISABLED);
    }
    lv_label_set_text(state_status_, view_model_.status().c_str());
    lv_obj_set_style_text_color(state_status_,
                                lv_color_hex(view_model_.state_status_error() ? kColorDestructive :
                                                 (pending ? kColorMutedText : kColorSuccess)), 0);
#if LV_USE_DRAW_OPENGLES
    if (!gpu_frame_.ready() && !gpu_error_reported_ && view_model_.state() != NesUiState::error) {
        gpu_error_reported_ = true;
        if (callbacks_.show_error) callbacks_.show_error("GPU unavailable; see application log");
    }
#endif
}

void GameView::keyboard_input(std::uint32_t key, bool pressed)
{
    if (!accepts_input()) return;
    // SDL's text keypad can synthesize a release with no key value.
    if (!pressed && key == 0) {
        clear_keyboard();
        return;
    }
#if defined(CM0_APP_SIMULATOR)
    // Physical SDL state owns releases when the text keypad is active.
    if (!pressed && SDL_WasInit(SDL_INIT_VIDEO) != 0) return;
#endif
    const auto binding = view_model_.keymap().binding(key);
    if (!binding.valid) return;
    const auto bit = 1U << static_cast<unsigned>(binding.button);
    auto &mask = binding.rapid ? keyboard_rapid_mask_ : keyboard_pressed_mask_;
    if (pressed) mask |= bit;
    else mask &= ~bit;
    sync_controls();
}

void GameView::tick()
{
    if (!game_panel_) return;
    update_host_keyboard();
    update_multitouch();
    if (lv_obj_has_flag(game_panel_, LV_OBJ_FLAG_HIDDEN) ||
        !view_model_.copy_latest_frame(frame_buffer_.data(), frame_buffer_.size())) return;
#if LV_USE_DRAW_OPENGLES
    bool uploaded;
    {
        nes_performance::Scope measured(nes_performance::Stage::gpu_upload);
        uploaded = gpu_frame_.update(frame_buffer_);
    }
    if (!uploaded) {
        view_model_.stop_rom();
        gpu_error_reported_ = true;
        set_visible(false);
        if (callbacks_.navigate) callbacks_.navigate(NesPage::library);
        if (callbacks_.show_error) callbacks_.show_error("GPU unavailable; see application log");
        return;
    }
#else
    {
        nes_performance::Scope measured(nes_performance::Stage::frame_prepare);
        scaled_frame_.update(frame_buffer_);
    }
#endif
    if (lv_obj_has_flag(frame_image_, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(frame_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(frame_placeholder_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_invalidate(frame_image_);
    // Present each delivered frame without waiting for the default refresh timer.
    if (auto *refresh = lv_display_get_refr_timer(lv_obj_get_display(frame_image_))) lv_timer_ready(refresh);
}

void GameView::stop_event(lv_event_t *event)
{
    auto *view = static_cast<GameView *>(lv_event_get_user_data(event));
    if (!view || !view->accepts_input()) return;
    view->view_model_.stop_rom();
    view->update_status();
    if (view->callbacks_.state_changed) view->callbacks_.state_changed();
}

void GameView::state_event(lv_event_t *event)
{
    auto *view = static_cast<GameView *>(lv_event_get_user_data(event));
    if (!view || !view->accepts_input()) return;
    if (lv_event_get_target(event) == view->save_state_button_) view->view_model_.save_state();
    else view->view_model_.load_state();
    view->update_status();
    if (view->callbacks_.state_changed) view->callbacks_.state_changed();
}

void GameView::control_event(lv_event_t *event)
{
    auto *context = static_cast<ControlContext *>(lv_event_get_user_data(event));
    if (!context || !context->view || !context->view->accepts_input()) return;
    const auto bit = static_cast<unsigned>(context->button);
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) context->view->lvgl_pressed_mask_ |= 1U << bit;
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) context->view->lvgl_pressed_mask_ &= ~(1U << bit);
    context->view->sync_controls();
}

lv_obj_t *GameView::create_control_button(lv_obj_t *parent, const char *text, NesButton button, std::uint32_t color,
                                         std::uint32_t text_color)
{
    static const char *const names[] = {"nes_control_up",    "nes_control_down",   "nes_control_left",
                                        "nes_control_right", "nes_control_select", "nes_control_start",
                                        "nes_control_a",     "nes_control_b"};
    const auto index                 = static_cast<std::size_t>(button);
    control_contexts_[index]         = {this, button};
    auto *control                    = lv_button_create(parent);
    lv_obj_set_name_static(control, names[index]);
    // Directional and system buttons share the Save button's flat treatment.
    // A/B keep their colored circular controls.
    const bool save_style = index <= static_cast<std::size_t>(NesButton::start);
    lv_obj_set_style_radius(control, save_style ? 14 : LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(control, save_style ? 0 : 1, 0);
    if (!save_style) lv_obj_set_style_border_color(control, lv_color_hex(kColorScreenBorder), 0);
    lv_obj_set_style_bg_color(control, save_style ? lv_color_hex(kColorControl) : lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(control, lv_color_hex(kColorControlPressed), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(control, 0, 0);
    lv_obj_add_event_cb(control, control_event, LV_EVENT_PRESSED, &control_contexts_[index]);
    lv_obj_add_event_cb(control, control_event, LV_EVENT_RELEASED, &control_contexts_[index]);
    lv_obj_add_event_cb(control, control_event, LV_EVENT_PRESS_LOST, &control_contexts_[index]);
    auto *label = lv_label_create(control);
    lv_label_set_text(label, text);
    set_font(label, 28);
    lv_obj_set_style_text_color(label, lv_color_hex(save_style ? 0xffffff : text_color), 0);
    lv_obj_center(label);
    control_buttons_[index] = control;
    return control;
}

lv_obj_t *GameView::create(lv_obj_t *parent)
{
    if (game_panel_) return game_panel_;
    parent_ = parent;
    game_panel_ = lv_obj_create(parent);
    lv_obj_set_name_static(game_panel_, "nes_game");
    lv_obj_set_size(game_panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(game_panel_, 0, 0);
    lv_obj_set_style_radius(game_panel_, 0, 0);
    lv_obj_set_style_bg_color(game_panel_, lv_color_hex(kColorGameBackground), 0);
    lv_obj_set_style_bg_opa(game_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(game_panel_, 0, 0);
    lv_obj_set_style_pad_row(game_panel_, 0, 0);
    lv_obj_set_flex_flow(game_panel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(game_panel_, LV_OBJ_FLAG_SCROLLABLE);

    state_toolbar_ = lv_obj_create(game_panel_);
    lv_obj_set_name_static(state_toolbar_, "nes_state_toolbar");
    make_transparent(state_toolbar_);
    lv_obj_set_size(state_toolbar_, LV_PCT(100), kStateToolbarHeight);
    lv_obj_set_style_pad_hor(state_toolbar_, 12, 0);
    lv_obj_set_style_pad_column(state_toolbar_, 10, 0);
    lv_obj_set_flex_flow(state_toolbar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state_toolbar_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    state_status_ = lv_label_create(state_toolbar_);
    lv_obj_set_name_static(state_status_, "nes_state_status");
    lv_obj_set_width(state_status_, 0);
    lv_obj_set_flex_grow(state_status_, 1);
    lv_label_set_long_mode(state_status_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(state_status_, "");
    set_font(state_status_, 16);
    lv_obj_set_style_text_color(state_status_, lv_color_hex(kColorSuccess), 0);

    game_content_ = lv_obj_create(game_panel_);
    lv_obj_set_size(game_content_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(game_content_, 1);
    make_transparent(game_content_);

    dpad_ = lv_obj_create(game_content_);
    make_transparent(dpad_);
    lv_obj_set_size(dpad_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(dpad_, LV_LAYOUT_GRID);
    auto *up    = create_control_button(dpad_, LV_SYMBOL_UP, NesButton::up, kColorControl);
    auto *down  = create_control_button(dpad_, LV_SYMBOL_DOWN, NesButton::down, kColorControl);
    auto *left  = create_control_button(dpad_, LV_SYMBOL_LEFT, NesButton::left, kColorControl);
    auto *right = create_control_button(dpad_, LV_SYMBOL_RIGHT, NesButton::right, kColorControl);
    lv_obj_set_grid_cell(up, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(left, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_grid_cell(right, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_grid_cell(down, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 2, 1);

    game_display_ = lv_obj_create(game_content_);
    make_transparent(game_display_);
    lv_obj_set_flex_flow(game_display_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(game_display_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(game_display_, control_size(8), 0);

    frame_panel_ = lv_obj_create(game_display_);
    lv_obj_set_name_static(frame_panel_, "nes_frame");
    lv_obj_set_style_radius(frame_panel_, 14, 0);
    lv_obj_set_style_border_width(frame_panel_, 0, 0);
    // lv_obj_set_style_border_color(frame_panel_, lv_color_hex(kColorScreenBorder), 0);
    lv_obj_set_style_bg_color(frame_panel_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(frame_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(frame_panel_, 0, 0);
    lv_obj_clear_flag(frame_panel_, LV_OBJ_FLAG_SCROLLABLE);

#if LV_USE_DRAW_OPENGLES
    frame_image_ = lv_3dtexture_create(frame_panel_);
    make_transparent(frame_image_);
    if (gpu_frame_.initialize()) {
        lv_3dtexture_set_src(frame_image_, gpu_frame_.texture_id());
        lv_3dtexture_set_flip(frame_image_, false, false);
    }
#else
    frame_image_ = lv_image_create(frame_panel_);
    lv_image_set_antialias(frame_image_, false);
#endif
    lv_obj_set_name_static(frame_image_, "nes_frame_image");
    lv_obj_set_size(frame_image_, NesEmulator::frame_width, NesEmulator::frame_height);
    lv_obj_add_flag(frame_image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(frame_image_);

    frame_placeholder_ = lv_label_create(frame_panel_);
    lv_label_set_text(frame_placeholder_, "NES");
    set_font(frame_placeholder_, 36);
    lv_obj_set_style_text_color(frame_placeholder_, lv_color_hex(kColorAccent), 0);
    lv_obj_center(frame_placeholder_);

    system_controls_ = lv_obj_create(game_content_);
    lv_obj_set_name_static(system_controls_, "nes_system_controls");
    make_transparent(system_controls_);
    lv_obj_set_height(system_controls_, control_size(48));
    lv_obj_set_flex_flow(system_controls_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(system_controls_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(system_controls_, control_size(10), 0);

    const auto state_button = [&](const char *text, const char *name) {
        auto *button = components::create_button(system_controls_, text, state_event, this);
        lv_obj_set_name_static(button, name);
        lv_obj_set_size(button, control_size(kStateButtonWidth), control_size(44));
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(kColorControl), 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(kColorControlPressed), LV_STATE_PRESSED);
        lv_obj_set_style_opa(button, LV_OPA_50, LV_STATE_DISABLED);
        set_font(lv_obj_get_child(button, 0), 20);
        lv_obj_set_style_text_color(lv_obj_get_child(button, 0), lv_color_hex(0xffffff), 0);
        return button;
    };
    save_state_button_ = state_button("Save", "nes_save_state");
    load_state_button_ = state_button("Load", "nes_load_state");

    auto *select = create_control_button(system_controls_, "Select", NesButton::select, kColorControl);
    auto *start  = create_control_button(system_controls_, "Start", NesButton::start, kColorControl);
    lv_obj_set_size(select, control_size(108), control_size(44));
    lv_obj_set_size(start, control_size(108), control_size(44));
    auto *stop = stop_button_ = lv_button_create(system_controls_);
    lv_obj_set_name_static(stop, "nes_stop");
    lv_obj_set_size(stop, control_size(52), control_size(44));
    lv_obj_set_style_radius(stop, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(stop, lv_color_hex(kColorDestructive), 0);
    lv_obj_set_style_bg_color(stop, lv_color_hex(0xd70015), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(stop, 0, 0);
    lv_obj_add_event_cb(stop, stop_event, LV_EVENT_CLICKED, this);
    auto *stop_label = lv_label_create(stop);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP);
    set_font(stop_label, 28);
    lv_obj_set_style_text_color(stop_label, lv_color_hex(0xffffff), 0);
    lv_obj_center(stop_label);

    action_buttons_ = lv_obj_create(game_content_);
    make_transparent(action_buttons_);
    lv_obj_set_layout(action_buttons_, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(action_buttons_, control_size(32), 0);
    lv_obj_set_style_pad_row(action_buttons_, control_size(32), 0);
    auto *button_b = create_control_button(action_buttons_, "B", NesButton::b, kColorWarning, 0x202124);
    auto *button_a = create_control_button(action_buttons_, "A", NesButton::a, kColorDestructive);
    lv_obj_set_style_radius(button_b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(button_a, LV_RADIUS_CIRCLE, 0);
    static const int32_t action_tracks[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(action_buttons_, action_tracks, action_tracks);
    // Put A in the upper-right and B in the lower-left.  Both center-to-center
    // deltas are identical, producing a 45-degree diagonal and a clear gap.
    lv_obj_set_grid_cell(button_a, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(button_b, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    resize_frame(NesEmulator::frame_width, NesEmulator::frame_height);
    if (auto *display = lv_obj_get_display(game_panel_)) {
        multitouch_.start(lv_display_get_horizontal_resolution(display), lv_display_get_vertical_resolution(display));
    }
    return game_panel_;
}

void GameView::sync_controls() noexcept
{
    const auto combined = lvgl_pressed_mask_ | multitouch_pressed_mask_ | keyboard_pressed_mask_;
    const auto changed  = combined ^ sent_pressed_mask_;
    for (unsigned bit = 0; bit < 8; ++bit) {
        if ((changed & (1U << bit)) != 0) {
            view_model_.set_button(static_cast<NesButton>(bit), (combined & (1U << bit)) != 0);
        }
    }
    sent_pressed_mask_ = combined;

    const auto rapid_changed = keyboard_rapid_mask_ ^ sent_rapid_mask_;
    for (unsigned bit = 0; bit < 8; ++bit) {
        if ((rapid_changed & (1U << bit)) != 0) {
            view_model_.set_rapid_button(static_cast<NesButton>(bit),
                                         (keyboard_rapid_mask_ & (1U << bit)) != 0);
        }
    }
    sent_rapid_mask_ = keyboard_rapid_mask_;
}

void GameView::update_multitouch()
{
    if (!accepts_input()) return;
    std::uint32_t mask = 0;
    for (const auto &point : multitouch_.points()) {
        for (unsigned bit = 0; bit < control_buttons_.size(); ++bit) {
            if (point_inside(point, control_buttons_[bit])) mask |= 1U << bit;
        }
    }
    if (mask != multitouch_pressed_mask_) {
        multitouch_pressed_mask_ = mask;
        sync_controls();
    }
}

void GameView::update_host_keyboard() noexcept
{
#if defined(CM0_APP_SIMULATOR)
    // LVGL's SDL keypad is text-input based.  It emits a synthetic release
    // immediately after every character, because SDL does not forward
    // KEYUP events through that driver.  Read SDL's physical key state here
    // so a held key remains held until the user actually releases it.
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) return;

    const auto *state = SDL_GetKeyboardState(nullptr);
    if (!state) return;

    const auto pressed = [&](std::uint32_t key) {
        key = nes_normalize_key(key);
        SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
        switch (key) {
        case LV_KEY_UP:
            scancode = SDL_SCANCODE_UP;
            break;
        case LV_KEY_DOWN:
            scancode = SDL_SCANCODE_DOWN;
            break;
        case LV_KEY_LEFT:
            scancode = SDL_SCANCODE_LEFT;
            break;
        case LV_KEY_RIGHT:
            scancode = SDL_SCANCODE_RIGHT;
            break;
        case LV_KEY_HOME:
            scancode = SDL_SCANCODE_HOME;
            break;
        case LV_KEY_END:
            scancode = SDL_SCANCODE_END;
            break;
        case LV_KEY_BACKSPACE:
            scancode = SDL_SCANCODE_BACKSPACE;
            break;
        case LV_KEY_DEL:
            scancode = SDL_SCANCODE_DELETE;
            break;
        case LV_KEY_ESC:
            scancode = SDL_SCANCODE_ESCAPE;
            break;
        case LV_KEY_ENTER:
            scancode = SDL_SCANCODE_RETURN;
            break;
        case LV_KEY_NEXT:
            scancode = SDL_SCANCODE_TAB;
            break;
        case LV_KEY_PREV:
            scancode = SDL_SCANCODE_PAGEUP;
            break;
        default:
            break;
        }
        if (scancode == SDL_SCANCODE_UNKNOWN) {
            scancode = SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(key));
        }
        return scancode != SDL_SCANCODE_UNKNOWN && state[scancode] != 0;
    };

    std::uint32_t regular = 0;
    std::uint32_t rapid   = 0;
    const auto &map       = view_model_.keymap();
    for (std::size_t index = 0; index < nes_mapping_action_count; ++index) {
        const auto action = static_cast<NesMappingAction>(index);
        if (!pressed(map.key(action))) continue;
        const auto binding = map.binding(map.key(action));
        if (!binding.valid) continue;
        const auto bit = 1U << static_cast<unsigned>(binding.button);
        (binding.rapid ? rapid : regular) |= bit;
    }

    if (accepts_input()) {
        if (regular != keyboard_pressed_mask_ || rapid != keyboard_rapid_mask_) {
            keyboard_pressed_mask_ = regular;
            keyboard_rapid_mask_   = rapid;
            sync_controls();
        }
    } else if (keyboard_pressed_mask_ != 0 || keyboard_rapid_mask_ != 0) {
        keyboard_pressed_mask_ = 0;
        keyboard_rapid_mask_   = 0;
        sync_controls();
    }
#endif
}

void GameView::apply_layout() noexcept
{
    if (!parent_ || !game_panel_) return;
    lv_obj_update_layout(parent_);
    const int width       = lv_obj_get_content_width(parent_);
    // The page fills its parent; use the parent dimensions while flex layout settles.
    const int body_height = lv_obj_get_content_height(parent_) - kStateToolbarHeight;
    const bool landscape  = width >= 800 && width > lv_obj_get_content_height(parent_);

    static const int32_t landscape_columns[] = {LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_rows[]    = {LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    static const int32_t portrait_columns[]  = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t portrait_rows[]     = {LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_CONTENT,
                                              LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

    const int padding = landscape ? 12 : 18;
    const int bottom_padding = landscape ? 12 : 44;
    // Fit the system row independently so portrait Save/Load buttons do not
    // force the direction pad and A/B buttons to shrink with them.
    const int control_width_budget = width - 2 * padding - 8;
    constexpr int system_base_width = 2 * kStateButtonWidth + 2 * 108 + 52 + 4 * kSystemButtonGap;
    const float system_scale = std::max(0.5F, std::min(kControlScale,
                                                       static_cast<float>(control_width_budget) / system_base_width));
    const float system_button_scale = system_scale * kSystemControlScale;
    const int system_gap = control_size(kSystemButtonGap, system_button_scale);
    const int system_height = std::max(44, control_size(44, system_button_scale));
    const int state_width = control_size(kStateButtonWidth, system_button_scale);
    const int primary_width = control_size(108, system_button_scale);
    const int stop_width = control_size(52, system_button_scale);
    const int system_width = 2 * state_width + 2 * primary_width + stop_width + 4 * system_gap;

    // Prefer 1.6x gameplay controls and reserve room for the full diagonal A/B
    // group, the independent system row, and the accumulated rounding of gaps.
    const int control_height_budget = body_height - padding - bottom_padding - system_height - 8;
    float control_scale = kControlScale;
    if (landscape) {
        constexpr int base_side_controls = 3 * 68 + 2 * 6 + 2 * 82 + 32 + 2 * 14;
        control_scale = std::min(control_scale, static_cast<float>(control_width_budget - NesEmulator::frame_width) /
                                                   base_side_controls);
        control_scale = std::min(control_scale, static_cast<float>(control_height_budget) / (3 * 68 + 2 * 6));
        control_scale = std::min(control_scale,
                                 static_cast<float>(control_height_budget - NesEmulator::frame_height) / (48 + 8));
    } else {
        constexpr int base_controls_height = 3 * 78 + 2 * 6 + (2 * 94 + 32) + 3 * 18;
        control_scale = std::min(control_scale, static_cast<float>(control_width_budget) / (3 * 78 + 2 * 6));
        control_scale = std::min(control_scale,
                                 static_cast<float>(control_height_budget - NesEmulator::frame_height) /
                                     base_controls_height);
    }
    control_scale = std::max(0.5F, control_scale);
    const int control_font = control_scale >= 1.4F ? 28 : (control_scale >= 1.0F ? 22 :
                                                          (control_scale >= 0.75F ? 18 : 14));
    for (auto *button : control_buttons_) set_font(lv_obj_get_child(button, 0), control_font);
    const int system_font = system_button_scale >= 1.4F ? 28 : (system_button_scale >= 1.0F ? 22 : 18);
    for (auto *button : {save_state_button_, load_state_button_, stop_button_,
                         control_buttons_[static_cast<std::size_t>(NesButton::select)],
                         control_buttons_[static_cast<std::size_t>(NesButton::start)]}) {
        set_font(lv_obj_get_child(button, 0), system_font);
    }
    const int dpad_size = control_size(landscape ? 68 : 78, control_scale);
    const int dpad_gap = control_size(6, control_scale);
    const int dpad_extent = 3 * dpad_size + 2 * dpad_gap;
    const int action_size = control_size(landscape ? 82 : 94, control_scale);
    const int action_gap = control_size(32, control_scale);
    const int action_extent = 2 * action_size + action_gap;
    const int group_gap = control_size(landscape ? 14 : 18, control_scale);

    lv_obj_set_grid_dsc_array(game_content_, landscape ? landscape_columns : portrait_columns,
                              landscape ? landscape_rows : portrait_rows);
    lv_obj_set_style_pad_all(game_content_, padding, 0);
    lv_obj_set_style_pad_bottom(game_content_, bottom_padding, 0);
    lv_obj_set_style_pad_row(game_content_, group_gap, 0);
    lv_obj_set_style_pad_column(game_content_, landscape ? group_gap : 0, 0);

    if (landscape) {
        lv_obj_set_grid_cell(dpad_, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(game_display_, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(action_buttons_, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(system_controls_, LV_GRID_ALIGN_CENTER, 0, 3, LV_GRID_ALIGN_CENTER, 1, 1);
    } else {
        lv_obj_set_grid_cell(game_display_, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 0, 1);
        lv_obj_set_grid_cell(dpad_, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 1, 1);
        lv_obj_set_grid_cell(action_buttons_, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 2, 1);
        lv_obj_set_grid_cell(system_controls_, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 3, 1);
    }

    dpad_tracks_ = {dpad_size, dpad_size, dpad_size, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(dpad_, dpad_tracks_.data(), dpad_tracks_.data());
    lv_obj_mark_layout_as_dirty(dpad_);
    lv_obj_set_style_pad_row(dpad_, dpad_gap, 0);
    lv_obj_set_style_pad_column(dpad_, dpad_gap, 0);

    lv_obj_set_style_pad_column(action_buttons_, action_gap, 0);
    lv_obj_set_style_pad_row(action_buttons_, action_gap, 0);
    lv_obj_set_size(action_buttons_, action_extent, action_extent);
    lv_obj_set_size(control_buttons_[static_cast<std::size_t>(NesButton::a)], action_size, action_size);
    lv_obj_set_size(control_buttons_[static_cast<std::size_t>(NesButton::b)], action_size, action_size);

    lv_obj_set_style_pad_column(system_controls_, system_gap, 0);
    lv_obj_set_size(system_controls_, system_width, system_height);
    lv_obj_set_size(save_state_button_, state_width, system_height);
    lv_obj_set_size(load_state_button_, state_width, system_height);
    for (const auto button : {NesButton::select, NesButton::start}) {
        lv_obj_set_size(control_buttons_[static_cast<std::size_t>(button)], primary_width, system_height);
    }
    lv_obj_set_size(stop_button_, stop_width, system_height);

    int available_height = body_height - padding - bottom_padding - system_height - group_gap;
    int available_width = std::min(width - 2 * padding - 4, 528);
    if (landscape) {
        available_width = width - dpad_extent - action_extent - 2 * padding - 2 * group_gap;
    } else {
        available_height -= dpad_extent + action_extent + 2 * group_gap;
    }
    int frame_width = std::min(available_width,
                               available_height * NesEmulator::frame_width / NesEmulator::frame_height);
    int frame_height = frame_width * NesEmulator::frame_height / NesEmulator::frame_width;
    frame_width  = std::max(frame_width, 256);
    frame_height = std::max(frame_height, 240);

    lv_obj_set_size(frame_panel_, frame_width, frame_height);
    const auto scale = static_cast<std::uint32_t>(std::min(frame_width * LV_SCALE_NONE / NesEmulator::frame_width,
                                                           frame_height * LV_SCALE_NONE / NesEmulator::frame_height));
    resize_frame(NesEmulator::frame_width * static_cast<int>(scale) / LV_SCALE_NONE,
                 NesEmulator::frame_height * static_cast<int>(scale) / LV_SCALE_NONE);
    lv_obj_set_size(game_display_, frame_width, frame_height);

    if (auto *display = lv_obj_get_display(parent_)) {
        multitouch_.set_display_size(lv_display_get_horizontal_resolution(display),
                                     lv_display_get_vertical_resolution(display));
    }
}

void GameView::resize_frame(int width, int height)
{
#if LV_USE_DRAW_OPENGLES
    lv_obj_set_size(frame_image_, width, height);
#else
    if (!scaled_frame_.resize(width, height)) return;
    scaled_frame_.update(frame_buffer_);
    frame_descriptor_.header.magic  = LV_IMAGE_HEADER_MAGIC;
    frame_descriptor_.header.cf     = LV_COLOR_FORMAT_XRGB8888;
    frame_descriptor_.header.flags  = LV_IMAGE_FLAGS_MODIFIABLE;
    frame_descriptor_.header.w      = width;
    frame_descriptor_.header.h      = height;
    frame_descriptor_.header.stride = width * sizeof(std::uint32_t);
    frame_descriptor_.data_size     = scaled_frame_.pixels().size() * sizeof(std::uint32_t);
    frame_descriptor_.data          = reinterpret_cast<const std::uint8_t *>(scaled_frame_.pixels().data());
    lv_image_set_src(frame_image_, &frame_descriptor_);
    lv_obj_set_size(frame_image_, width, height);
#endif
}
