#include "pages/nes/nes_view_model.hpp"

#include <utility>

NesViewModel::NesViewModel()
{
    initialize();
}

NesViewModel::NesViewModel(std::vector<std::string> search_paths) : library_(std::move(search_paths))
{
    initialize();
}

void NesViewModel::initialize()
{
    rom_directory_ = library_.primary_path();
    refresh_library();
}

void NesViewModel::refresh_library()
{
    if (emulator_.running()) return;
    library_.scan();
    rom_count_.set(static_cast<std::int32_t>(library_.roms().size()));
    selected_index_ = 0;
    state_          = NesUiState::library;
    if (library_.roms().empty()) {
        status_ = "No ROM files found";
    } else if (library_.roms().size() == 1) {
        status_ = "1 ROM ready";
    } else {
        status_ = std::to_string(library_.roms().size()) + " ROMs ready";
    }
}

bool NesViewModel::start_rom(std::size_t index)
{
    if (index >= library_.roms().size()) return false;
    selected_index_ = index;
    const auto &rom = library_.roms()[index];
    if (!rom.valid_header) {
        state_  = NesUiState::error;
        status_ = "Invalid iNES ROM";
        return false;
    }
    if (!emulator_.start(rom.path)) {
        state_  = NesUiState::error;
        status_ = "Could not start emulator";
        return false;
    }
    state_  = NesUiState::running;
    status_ = "Running " + rom.title;
    return true;
}

bool NesViewModel::delete_rom(std::size_t index)
{
    if (emulator_.running() || state_ == NesUiState::running || state_ == NesUiState::stopping) {
        status_ = "Close the game before deleting ROMs";
        return false;
    }

    std::error_code error;
    if (!library_.delete_rom(index, error)) {
        state_  = NesUiState::error;
        status_ = "Could not delete ROM: " + error.message();
        return false;
    }

    rom_count_.set(static_cast<std::int32_t>(library_.roms().size()));
    selected_index_ = 0;
    state_          = NesUiState::library;
    status_         = library_.roms().empty() ? "No ROM files found" : "ROM deleted";
    return true;
}

void NesViewModel::stop_rom() noexcept
{
    if (!emulator_.running()) return;
    emulator_.stop();
    state_  = NesUiState::stopping;
    status_ = "Stopping emulator...";
}

void NesViewModel::set_button(NesButton button, bool pressed) noexcept
{
    emulator_.set_button(button, pressed);
}

bool NesViewModel::poll()
{
    int result = 0;
    if (!emulator_.consume_finished(result)) return false;
    state_  = result == 0 ? NesUiState::library : NesUiState::error;
    status_ = result == 0 ? "Game closed" : "Emulator exited with code " + std::to_string(result);
    return true;
}

const std::vector<RomInfo> &NesViewModel::roms() const noexcept
{
    return library_.roms();
}

const RomInfo *NesViewModel::selected_rom() const noexcept
{
    return selected_index_ < library_.roms().size() ? &library_.roms()[selected_index_] : nullptr;
}

std::size_t NesViewModel::selected_index() const noexcept
{
    return selected_index_;
}

NesUiState NesViewModel::state() const noexcept
{
    return state_;
}

const std::string &NesViewModel::status() const noexcept
{
    return status_;
}

const std::string &NesViewModel::rom_directory() const noexcept
{
    return rom_directory_;
}

lv_subject_t *NesViewModel::rom_count_subject() noexcept
{
    return rom_count_.get();
}

bool NesViewModel::copy_latest_frame(std::uint16_t *destination, std::size_t pixels) noexcept
{
    return emulator_.copy_latest_frame(destination, pixels);
}
