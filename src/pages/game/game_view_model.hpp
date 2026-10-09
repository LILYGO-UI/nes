#ifndef LILYGO_UI_NES_PAGES_GAME_GAME_VIEW_MODEL_HPP
#define LILYGO_UI_NES_PAGES_GAME_GAME_VIEW_MODEL_HPP

#include "domain/rom_library.hpp"
#include "emulator/nes_emulator.hpp"
#include "pages/nes/nes_ui_state.hpp"
#include "pages/settings/settings_view_model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

class GameViewModel {
public:
    GameViewModel(NesUiStatus &ui_status, const SettingsViewModel &settings) noexcept;

    bool start_rom(const RomInfo &rom);
    void stop_rom() noexcept;
    bool save_state();
    bool load_state();
    [[nodiscard]] bool state_operation_pending() const noexcept;
    [[nodiscard]] bool state_status_error() const noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] NesUiState state() const noexcept;
    [[nodiscard]] const std::string &status() const noexcept;
    [[nodiscard]] std::uint64_t error_revision() const noexcept;
    void set_button(NesButton button, bool pressed) noexcept;
    void set_rapid_button(NesButton button, bool pressed) noexcept;
    [[nodiscard]] const NesKeyMap &keymap() const noexcept;
    bool poll();
    bool copy_latest_frame(std::uint16_t *destination, std::size_t pixels) noexcept;

private:
    bool request_state_operation(bool save);

    NesUiStatus &ui_status_;
    const SettingsViewModel &settings_;
    NesEmulator emulator_;
    std::string running_rom_path_;
    std::string state_file_path_;
    bool state_request_pending_ = false;
    bool state_status_error_ = false;
};

#endif
