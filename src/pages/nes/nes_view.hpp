#ifndef LILYGO_UI_NES_PAGES_NES_NES_VIEW_HPP
#define LILYGO_UI_NES_PAGES_NES_NES_VIEW_HPP

#include "pages/nes/nes_view_model.hpp"
#include "pages/nes/page_navigation.hpp"
#include "pages/library/library_view.hpp"
#include "pages/rom_browser/rom_browser_view.hpp"
#include "pages/game/game_view.hpp"
#include "pages/settings/settings_view.hpp"
#include "pages/help/help_view.hpp"

#include <lvgl.h>

// Application container: page lifetime, navigation, keyboard routing and errors.
// Every page owns its controls and binds only to its own ViewModel.
class NesView {
public:
    explicit NesView(NesViewModel &view_model) noexcept;
    ~NesView();
    NesView(const NesView &) = delete;
    NesView &operator=(const NesView &) = delete;

    lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;

private:
    void navigate(NesPage page);
    void apply_layout() noexcept;
    void update_status();
    void create_error_dialog();
    void show_error(const char *message);
    void attach_keyboard() noexcept;
    void suspend_keyboard() noexcept;
    void resume_keyboard() noexcept;
    static void size_changed_event(lv_event_t *event);
    static void keyboard_event(lv_event_t *event);
    static void keyboard_deleted_event(lv_event_t *event);
    static void timer_event(lv_timer_t *timer);
    static void error_timeout_event(lv_timer_t *timer);

    NesViewModel &view_model_;
    NesPageCallbacks callbacks_;
    LibraryView library_;
    GameView game_;
    RomBrowserView browser_;
    SettingsView settings_;
    HelpView help_;
    NesPage page_ = NesPage::library;
    lv_obj_t *surface_ = nullptr;
    lv_obj_t *body_ = nullptr;
    lv_obj_t *error_overlay_ = nullptr;
    lv_obj_t *error_message_ = nullptr;
    lv_timer_t *timer_ = nullptr;
    lv_timer_t *error_timer_ = nullptr;
    std::uint64_t shown_error_revision_ = 0;
    lv_indev_t *keyboard_ = nullptr;
    lv_group_t *keyboard_group_ = nullptr;
    bool keyboard_suspended_ = false;
};

#endif
