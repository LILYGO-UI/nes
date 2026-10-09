#include "pages/library/library_view_model.hpp"

#include <filesystem>
#include <utility>

LibraryViewModel::LibraryViewModel(NesUiStatus &ui_status, GameViewModel &game)
    : LibraryViewModel(ui_status, game, RomLibrary::default_search_paths())
{
}

LibraryViewModel::LibraryViewModel(NesUiStatus &ui_status, GameViewModel &game,
                                 std::vector<std::string> search_paths)
    : ui_status_(ui_status), game_(game), library_(std::move(search_paths)), rom_directory_(library_.primary_path())
{
    refresh_library();
}

void LibraryViewModel::refresh_library()
{
    if (game_.active()) return;
    std::error_code directory_error;
    if (!rom_directory_.empty()) std::filesystem::create_directories(rom_directory_, directory_error);
    library_.scan();
    selected_index_ = 0;
    ui_status_.state = NesUiState::library;
    if (rom_directory_.empty()) {
        ui_status_.set_error("HOME directory is unavailable");
    } else if (directory_error) {
        ui_status_.set_error("Could not create ROM folder: " + directory_error.message());
    } else {
        ui_status_.status.clear();
    }
}

bool LibraryViewModel::start_rom(std::size_t index)
{
    if (game_.active() || index >= library_.roms().size()) return false;
    selected_index_ = index;
    return game_.start_rom(library_.roms()[index]);
}

bool LibraryViewModel::import_rom(const std::string &source_path)
{
    if (game_.active()) {
        ui_status_.status = "Close the game before adding ROMs";
        return false;
    }

    std::error_code error;
    if (!library_.import_rom(source_path, error)) {
        if (rom_directory_.empty()) {
            ui_status_.set_error("Could not add ROM: HOME directory is unavailable");
        } else if (error == std::errc::file_exists) {
            ui_status_.set_error("A ROM with this filename already exists");
        } else if (error == std::errc::illegal_byte_sequence) {
            ui_status_.set_error("Invalid iNES ROM");
        } else if (error == std::errc::value_too_large) {
            ui_status_.set_error("ROM library is full (" + std::to_string(RomLibrary::maximum_roms) + " files)");
        } else {
            ui_status_.set_error("Could not add ROM: " + error.message());
        }
        return false;
    }

    selected_index_ = 0;
    ui_status_.state = NesUiState::library;
    ui_status_.status = "ROM added";
    return true;
}

bool LibraryViewModel::delete_rom(std::size_t index)
{
    if (game_.active()) {
        ui_status_.status = "Close the game before deleting ROMs";
        return false;
    }

    std::error_code error;
    if (!library_.delete_rom(index, error)) {
        ui_status_.set_error("Could not delete ROM: " + error.message());
        return false;
    }

    selected_index_ = 0;
    ui_status_.state = NesUiState::library;
    ui_status_.status = library_.roms().empty() ? "" : "ROM deleted";
    return true;
}

const std::vector<RomInfo> &LibraryViewModel::roms() const noexcept
{
    return library_.roms();
}

const RomInfo *LibraryViewModel::selected_rom() const noexcept
{
    return selected_index_ < library_.roms().size() ? &library_.roms()[selected_index_] : nullptr;
}

std::size_t LibraryViewModel::selected_index() const noexcept
{
    return selected_index_;
}

const std::string &LibraryViewModel::rom_directory() const noexcept
{
    return rom_directory_;
}

NesUiState LibraryViewModel::state() const noexcept
{
    return ui_status_.state;
}

const std::string &LibraryViewModel::status() const noexcept
{
    return ui_status_.status;
}

std::uint64_t LibraryViewModel::error_revision() const noexcept
{
    return ui_status_.error_revision;
}
