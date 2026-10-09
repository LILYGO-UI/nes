#ifndef LILYGO_UI_NES_PAGE_NAVIGATION_HPP
#define LILYGO_UI_NES_PAGE_NAVIGATION_HPP

#include <functional>

enum class NesPage { library, browser, settings, help, game };

// The application container owns navigation and shared error presentation.
// Pages receive commands instead of pointers to another page's LVGL objects.
struct NesPageCallbacks {
    std::function<void(NesPage)> navigate;
    std::function<void()> state_changed;
    std::function<bool()> can_start_game;
    std::function<void(const char *)> show_error;
    std::function<void()> library_changed;
};

#endif
