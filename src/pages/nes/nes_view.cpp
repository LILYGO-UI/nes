#include "pages/nes/nes_view.hpp"

#include "components/components.hpp"
#include "components/page_helpers.hpp"
#include "emulator/nes_performance.hpp"

#include <cm0/typography.h>

namespace {
using namespace page_ui;
constexpr std::uint32_t kFramePollPeriodMs = 16;
constexpr std::uint32_t kErrorTimeoutMs = 5000;
}  // namespace

NesView::NesView(NesViewModel &view_model) noexcept
    : view_model_(view_model),
      callbacks_{[this](NesPage page) { navigate(page); },
                 [this] { update_status(); },
                 [this] { return game_.can_start(); },
                 [this](const char *message) { show_error(message); },
                 [this] { library_.rebuild_rom_list(); }},
      library_(view_model.library(), callbacks_), game_(view_model.game(), callbacks_),
      browser_(view_model.browser(), callbacks_), settings_(view_model.settings(), callbacks_),
      help_(view_model.help(), callbacks_)
{
}

NesView::~NesView()
{
    destroy();
}

lv_obj_t *NesView::create(lv_obj_t *parent)
{
    if (surface_) return surface_;
    surface_ = components::create_page(parent);
    body_ = lv_obj_create(surface_);
    lv_obj_set_size(body_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(body_, 1);
    make_transparent(body_);

    library_.create(body_);
    game_.create(body_);
    browser_.create(body_);
    settings_.create(body_);
    help_.create(body_);
    create_error_dialog();
    library_.rebuild_rom_list();
    navigate(NesPage::library);
    lv_obj_update_layout(surface_);
    apply_layout();
    // Creating controls can trigger layout through the default focus group.
    // Subscribe only after every child View has finished construction.
    lv_obj_add_event_cb(body_, size_changed_event, LV_EVENT_SIZE_CHANGED, this);
    attach_keyboard();
    timer_ = lv_timer_create(timer_event, kFramePollPeriodMs, this);
    return surface_;
}

void NesView::destroy() noexcept
{
    if (timer_) lv_timer_delete(timer_);
    timer_ = nullptr;
    if (error_timer_) lv_timer_delete(error_timer_);
    error_timer_ = nullptr;
    resume_keyboard();
    if (keyboard_) {
        lv_indev_remove_event_cb_with_user_data(keyboard_, keyboard_event, this);
        lv_indev_remove_event_cb_with_user_data(keyboard_, keyboard_deleted_event, this);
    }
    keyboard_ = nullptr;
    keyboard_group_ = nullptr;
    keyboard_suspended_ = false;
    if (body_) lv_obj_remove_event_cb(body_, size_changed_event);
    // GameView stops the touch reader, requests stop and releases all buttons
    // before deleting its controls. The ViewModel outlives every child View.
    game_.destroy();
    help_.destroy();
    settings_.destroy();
    browser_.destroy();
    library_.destroy();
    if (surface_) lv_obj_delete(surface_);
    surface_ = nullptr;
    body_ = nullptr;
    error_overlay_ = nullptr;
    error_message_ = nullptr;
    shown_error_revision_ = 0;
    page_ = NesPage::library;
}

void NesView::navigate(NesPage page)
{
    if (page == NesPage::browser && page_ != NesPage::browser) {
        view_model_.browser().open_browser();
        browser_.rebuild_browser_list();
    }
    page_ = page;
    library_.set_visible(page == NesPage::library);
    game_.set_visible(page == NesPage::game);
    browser_.set_visible(page == NesPage::browser);
    settings_.set_visible(page == NesPage::settings);
    help_.set_visible(page == NesPage::help);
    apply_layout();
    update_status();
}

void NesView::apply_layout() noexcept
{
    if (!surface_ || !body_) return;
    lv_obj_update_layout(surface_);
    library_.apply_layout();
    game_.apply_layout();
}

void NesView::size_changed_event(lv_event_t *event)
{
    static_cast<NesView *>(lv_event_get_user_data(event))->apply_layout();
}

void NesView::update_status()
{
    game_.update_status();
    if (view_model_.state() == NesUiState::error && shown_error_revision_ != view_model_.error_revision()) {
        shown_error_revision_ = view_model_.error_revision();
        show_error(view_model_.status().c_str());
    }
}

void NesView::create_error_dialog()
{
    error_overlay_ = lv_obj_create(body_);
    lv_obj_set_name_static(error_overlay_, "nes_error_overlay");
    make_transparent(error_overlay_);
    lv_obj_set_size(error_overlay_, LV_PCT(100), LV_PCT(100));
    // A black scrim at 40% opacity keeps the current page visible behind the alert.
    lv_obj_set_style_bg_color(error_overlay_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(error_overlay_, LV_OPA_40, 0);
    lv_obj_add_flag(error_overlay_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(error_overlay_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(error_overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(error_overlay_);

    auto *dialog = lv_obj_create(error_overlay_);
    lv_obj_set_name_static(dialog, "nes_error_dialog");
    lv_obj_set_size(dialog, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(dialog, 520, 0);
    lv_obj_set_style_max_height(dialog, LV_PCT(90), 0);
    lv_obj_set_style_border_width(dialog, 0, 0);
    lv_obj_set_style_radius(dialog, 20, 0);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(components::color_container_background), 0);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(dialog, 24, 0);
    lv_obj_set_style_pad_row(dialog, 16, 0);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(dialog, LV_DIR_VER);
    lv_obj_center(dialog);

    auto *title = lv_label_create(dialog);
    lv_obj_set_name_static(title, "nes_error_title");
    lv_label_set_text(title, LV_SYMBOL_WARNING " Error");
    set_font(title, 28);
    lv_obj_set_style_text_color(title, lv_color_hex(components::color_destructive), 0);

    error_message_ = lv_label_create(dialog);
    lv_obj_set_name_static(error_message_, "nes_error_message");
    lv_obj_set_width(error_message_, LV_PCT(100));
    lv_label_set_long_mode(error_message_, LV_LABEL_LONG_WRAP);
    set_font(error_message_, 22);
    lv_obj_set_style_text_color(error_message_, lv_color_hex(components::color_text), 0);

    auto *hint = lv_label_create(dialog);
    lv_label_set_text(hint, "Closes automatically in 5 seconds.");
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    set_font(hint, 14);
    lv_obj_set_style_text_color(hint, lv_color_hex(components::color_secondary_text), 0);
}

void NesView::show_error(const char *message)
{
    if (!error_overlay_) return;
    lv_label_set_text(error_message_, message);
    lv_obj_scroll_to_y(lv_obj_get_parent(error_message_), 0, LV_ANIM_OFF);
    lv_obj_clear_flag(error_overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(error_overlay_);
    suspend_keyboard();
    if (error_timer_) lv_timer_delete(error_timer_);
    error_timer_ = lv_timer_create(error_timeout_event, kErrorTimeoutMs, this);
    lv_timer_set_repeat_count(error_timer_, 1);
}

void NesView::error_timeout_event(lv_timer_t *timer)
{
    auto *view = static_cast<NesView *>(lv_timer_get_user_data(timer));
    view->error_timer_ = nullptr;  // LVGL deletes this one-shot timer after the callback.
    lv_obj_add_flag(view->error_overlay_, LV_OBJ_FLAG_HIDDEN);
    view->resume_keyboard();
}

void NesView::attach_keyboard() noexcept
{
    if (keyboard_) return;
    for (auto *indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) != LV_INDEV_TYPE_KEYPAD) continue;
        keyboard_ = indev;
        lv_indev_add_event_cb(indev, keyboard_event, LV_EVENT_KEY, this);
        lv_indev_add_event_cb(indev, keyboard_deleted_event, LV_EVENT_DELETE, this);
        if (error_overlay_ && !lv_obj_has_flag(error_overlay_, LV_OBJ_FLAG_HIDDEN)) suspend_keyboard();
        return;
    }
}

void NesView::suspend_keyboard() noexcept
{
    game_.set_input_blocked(true);
    if (!keyboard_ || keyboard_suspended_) return;
    // Isolate the focus group: suppressing LV_EVENT_KEY alone still lets LVGL
    // route Enter's later CLICKED event and Tab navigation to underlying buttons.
    keyboard_group_ = lv_indev_get_group(keyboard_);
    keyboard_suspended_ = true;
    lv_indev_set_group(keyboard_, nullptr);
    lv_indev_reset(keyboard_, nullptr);
}

void NesView::resume_keyboard() noexcept
{
    game_.set_input_blocked(false);
    if (!keyboard_ || !keyboard_suspended_) return;
    lv_indev_set_group(keyboard_, keyboard_group_);
    lv_indev_wait_release(keyboard_);
    keyboard_group_ = nullptr;
    keyboard_suspended_ = false;
}

void NesView::keyboard_event(lv_event_t *event)
{
    auto *view = static_cast<NesView *>(lv_event_get_user_data(event));
    auto *indev = static_cast<lv_indev_t *>(lv_event_get_target(event));
    if (!view || !indev || indev != view->keyboard_) return;
    if (view->error_overlay_ && !lv_obj_has_flag(view->error_overlay_, LV_OBJ_FLAG_HIDDEN)) {
        lv_indev_stop_processing(indev);
        return;
    }
    const auto key = lv_indev_get_key(indev);
    const bool pressed = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;
    if (view->page_ == NesPage::settings && view->settings_.capturing()) {
        if (pressed && nes_valid_key(key)) {
            view->settings_.capture_mapping_key(key);
            lv_indev_stop_processing(indev);
        }
        return;
    }
    if (view->page_ == NesPage::game) view->game_.keyboard_input(key, pressed);
}

void NesView::keyboard_deleted_event(lv_event_t *event)
{
    auto *view = static_cast<NesView *>(lv_event_get_user_data(event));
    auto *indev = static_cast<lv_indev_t *>(lv_event_get_target(event));
    if (!view || indev != view->keyboard_) return;
    view->keyboard_ = nullptr;
    view->keyboard_group_ = nullptr;
    view->keyboard_suspended_ = false;
    view->game_.clear_keyboard();
}

void NesView::timer_event(lv_timer_t *timer)
{
    auto *view = static_cast<NesView *>(lv_timer_get_user_data(timer));
    nes_performance::report_if_due();
    view->attach_keyboard();
    view->game_.tick();
    if (view->view_model_.poll()) {
        const auto state = view->view_model_.state();
        if (state == NesUiState::library || state == NesUiState::error) {
            view->navigate(NesPage::library);
        } else {
            view->update_status();
        }
    }
}
