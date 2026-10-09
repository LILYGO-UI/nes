#include "pages/settings/settings_view_model.hpp"

SettingsViewModel::SettingsViewModel()
{
    std::error_code error;
    keymap_ = nes_load_key_map(error);
    cancel_capture();
}

NesKeyMap &SettingsViewModel::keymap() noexcept
{
    return keymap_;
}

const NesKeyMap &SettingsViewModel::keymap() const noexcept
{
    return keymap_;
}

NesKeyMapResult SettingsViewModel::set_key_mapping(NesMappingAction action, std::uint32_t key,
                                                 NesKeyMapConflictPolicy policy) noexcept
{
    return keymap_.set_key(action, key, policy);
}

bool SettingsViewModel::save_keymap(std::error_code &error) const
{
    return nes_save_key_map(keymap_, error);
}

void SettingsViewModel::begin_mapping_capture(NesMappingAction action)
{
    if (!nes_valid_mapping_action(action)) return;
    mapping_capture_ = action;
    status_ = std::string("Press a key for ") + nes_mapping_action_label(action) + ". Tap Back to cancel.";
    feedback_ = SettingsFeedback::action;
}

void SettingsViewModel::capture_mapping_key(std::uint32_t key)
{
    if (!mapping_capture_ || !nes_valid_key(key)) return;
    const auto result = set_key_mapping(*mapping_capture_, key, NesKeyMapConflictPolicy::reject);
    if (result == NesKeyMapResult::conflict) {
        const auto owner = keymap_.action_for(key);
        const auto owner_name = owner ? nes_mapping_action_label(*owner) : "another control";
        status_ = std::string("Key is already assigned to ") + owner_name + ". Choose another key.";
        feedback_ = SettingsFeedback::error;
        return;
    }
    if (result != NesKeyMapResult::applied) {
        status_ = "That key cannot be assigned. Choose another key.";
        feedback_ = SettingsFeedback::error;
        return;
    }

    std::error_code error;
    const bool saved = save_keymap(error);
    mapping_capture_.reset();
    if (saved) {
        status_ = "Key mapping updated.";
        feedback_ = SettingsFeedback::success;
    } else {
        status_ = std::string("Key mapping updated, but could not save: ") + error.message();
        feedback_ = SettingsFeedback::warning;
    }
}

void SettingsViewModel::cancel_capture()
{
    mapping_capture_.reset();
    status_ = "Tap a control, then press a key on the keyboard.";
    feedback_ = SettingsFeedback::neutral;
}

std::optional<NesMappingAction> SettingsViewModel::mapping_capture() const noexcept
{
    return mapping_capture_;
}

const std::string &SettingsViewModel::status() const noexcept
{
    return status_;
}

SettingsFeedback SettingsViewModel::feedback() const noexcept
{
    return feedback_;
}
