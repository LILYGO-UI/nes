#include "pages/game/game_view_model.hpp"

#include "domain/nes_paths.hpp"

GameViewModel::GameViewModel(NesUiStatus &ui_status, const SettingsViewModel &settings) noexcept
    : ui_status_(ui_status), settings_(settings)
{
}

bool GameViewModel::start_rom(const RomInfo &rom)
{
    if (active()) return false;
    if (!rom.valid_header) {
        ui_status_.set_error("Invalid iNES ROM");
        return false;
    }
    if (!emulator_.start(rom.path)) {
        ui_status_.set_error("Could not start emulator");
        return false;
    }
    ui_status_.state = NesUiState::running;
    // The running identity must survive library selection changes.
    running_rom_path_ = rom.path;
    state_file_path_ = nes_paths::state_path(running_rom_path_);
    state_request_pending_ = false;
    state_status_error_ = false;
    ui_status_.status.clear();
    return true;
}

void GameViewModel::stop_rom() noexcept
{
    if (!emulator_.running()) return;
    emulator_.stop();
    ui_status_.state = NesUiState::stopping;
    ui_status_.status = "Stopping emulator...";
}

bool GameViewModel::save_state()
{
    return request_state_operation(true);
}

bool GameViewModel::load_state()
{
    return request_state_operation(false);
}

bool GameViewModel::request_state_operation(bool save)
{
    if (ui_status_.state != NesUiState::running || state_request_pending_) return false;
    if (state_file_path_.empty()) {
        ui_status_.status = "Save folder unavailable: HOME is not set";
        state_status_error_ = true;
        return false;
    }
    const bool accepted = save ? emulator_.request_save_state(state_file_path_)
                               : emulator_.request_load_state(state_file_path_);
    if (!accepted) {
        ui_status_.status = "Could not request state operation";
        state_status_error_ = true;
        return false;
    }
    state_request_pending_ = true;
    state_status_error_ = false;
    ui_status_.status = save ? "Saving..." : "Loading...";
    return true;
}

bool GameViewModel::state_operation_pending() const noexcept
{
    return state_request_pending_;
}

bool GameViewModel::state_status_error() const noexcept
{
    return state_status_error_;
}

bool GameViewModel::active() const noexcept
{
    return emulator_.running() || ui_status_.state == NesUiState::running || ui_status_.state == NesUiState::stopping;
}

NesUiState GameViewModel::state() const noexcept
{
    return ui_status_.state;
}

const std::string &GameViewModel::status() const noexcept
{
    return ui_status_.status;
}

std::uint64_t GameViewModel::error_revision() const noexcept
{
    return ui_status_.error_revision;
}

void GameViewModel::set_button(NesButton button, bool pressed) noexcept
{
    emulator_.set_button(button, pressed);
}

void GameViewModel::set_rapid_button(NesButton button, bool pressed) noexcept
{
    emulator_.set_rapid_button(button, pressed);
}

const NesKeyMap &GameViewModel::keymap() const noexcept
{
    return settings_.keymap();
}

bool GameViewModel::poll()
{
    bool changed = false;
    NesStateResult state_result{};
    if (emulator_.consume_state_result(state_result)) {
        state_request_pending_ = false;
        if (ui_status_.state == NesUiState::running) {
            state_status_error_ = !state_result.success;
            ui_status_.status = state_result.message;
            if (ui_status_.status.empty()) {
                ui_status_.status = state_result.success
                                      ? (state_result.operation == NesStateOperation::save ? "State saved" : "State loaded")
                                      : "State operation failed";
            }
            changed = true;
        }
    }
    int result = 0;
    if (!emulator_.consume_finished(result)) return changed;
    running_rom_path_.clear();
    state_file_path_.clear();
    state_request_pending_ = false;
    state_status_error_ = false;
    if (result == 0) {
        ui_status_.state = NesUiState::library;
        ui_status_.status = "Game closed";
    } else {
        ui_status_.set_error("Emulator exited with code " + std::to_string(result));
    }
    return true;
}

bool GameViewModel::copy_latest_frame(std::uint16_t *destination, std::size_t pixels) noexcept
{
    return emulator_.copy_latest_frame(destination, pixels);
}
