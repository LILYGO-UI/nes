#ifndef LILYGO_UI_NES_PAGES_SETTINGS_SETTINGS_VIEW_MODEL_HPP
#define LILYGO_UI_NES_PAGES_SETTINGS_SETTINGS_VIEW_MODEL_HPP

#include "emulator/nes_input_mapping.hpp"

#include <optional>
#include <string>
#include <system_error>

enum class SettingsFeedback {
    neutral,
    action,
    error,
    success,
    warning,
};

class SettingsViewModel {
public:
    SettingsViewModel();

    [[nodiscard]] NesKeyMap &keymap() noexcept;
    [[nodiscard]] const NesKeyMap &keymap() const noexcept;
    NesKeyMapResult set_key_mapping(NesMappingAction action, std::uint32_t key,
                                    NesKeyMapConflictPolicy policy = NesKeyMapConflictPolicy::reject) noexcept;
    bool save_keymap(std::error_code &error) const;

    void begin_mapping_capture(NesMappingAction action);
    void capture_mapping_key(std::uint32_t key);
    void cancel_capture();
    [[nodiscard]] std::optional<NesMappingAction> mapping_capture() const noexcept;
    [[nodiscard]] const std::string &status() const noexcept;
    [[nodiscard]] SettingsFeedback feedback() const noexcept;

private:
    NesKeyMap keymap_;
    std::optional<NesMappingAction> mapping_capture_;
    std::string status_;
    SettingsFeedback feedback_ = SettingsFeedback::neutral;
};

#endif
