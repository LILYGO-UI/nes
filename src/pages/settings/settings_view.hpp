#ifndef LILYGO_UI_NES_PAGES_SETTINGS_SETTINGS_VIEW_HPP
#define LILYGO_UI_NES_PAGES_SETTINGS_SETTINGS_VIEW_HPP

#include "pages/nes/page_navigation.hpp"
#include "pages/settings/settings_view_model.hpp"

#include <lvgl.h>

#include <array>
#include <cstdint>

class SettingsView {
public:
    SettingsView(SettingsViewModel &view_model, const NesPageCallbacks &callbacks);

    lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;
    void set_visible(bool visible);
    [[nodiscard]] lv_obj_t *root() const noexcept { return settings_panel_; }
    void refresh_mapping_labels();
    void capture_mapping_key(std::uint32_t key);
    [[nodiscard]] bool capturing() const noexcept;

private:
    struct MappingContext {
        SettingsView *view = nullptr;
        NesMappingAction action = NesMappingAction::up;
    };

    static void settings_event(lv_event_t *event);
    static void mapping_event(lv_event_t *event);
    void refresh_status();

    SettingsViewModel &view_model_;
    NesPageCallbacks callbacks_;
    std::array<MappingContext, nes_mapping_action_count> mapping_contexts_{};
    std::array<lv_obj_t *, nes_mapping_action_count> mapping_key_labels_{};
    lv_obj_t *settings_panel_ = nullptr;
    lv_obj_t *settings_back_ = nullptr;
    lv_obj_t *settings_status_ = nullptr;
};

#endif
