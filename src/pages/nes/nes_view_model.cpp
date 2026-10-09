#include "pages/nes/nes_view_model.hpp"

#include "domain/nes_paths.hpp"

#include <utility>

NesViewModel::NesViewModel() : NesViewModel(RomLibrary::default_search_paths())
{
}

NesViewModel::NesViewModel(std::vector<std::string> search_paths)
    : game_(ui_status_, settings_), library_(ui_status_, game_, std::move(search_paths)), browser_(library_),
      help_(library_.rom_directory(), nes_paths::states_directory())
{
}

LibraryViewModel &NesViewModel::library() noexcept { return library_; }
const LibraryViewModel &NesViewModel::library() const noexcept { return library_; }
RomBrowserViewModel &NesViewModel::browser() noexcept { return browser_; }
const RomBrowserViewModel &NesViewModel::browser() const noexcept { return browser_; }
GameViewModel &NesViewModel::game() noexcept { return game_; }
const GameViewModel &NesViewModel::game() const noexcept { return game_; }
SettingsViewModel &NesViewModel::settings() noexcept { return settings_; }
const SettingsViewModel &NesViewModel::settings() const noexcept { return settings_; }
HelpViewModel &NesViewModel::help() noexcept { return help_; }
const HelpViewModel &NesViewModel::help() const noexcept { return help_; }

void NesViewModel::refresh_library() { library_.refresh_library(); }
bool NesViewModel::start_rom(std::size_t index) { return library_.start_rom(index); }
bool NesViewModel::import_rom(const std::string &path) { return library_.import_rom(path); }
bool NesViewModel::delete_rom(std::size_t index) { return library_.delete_rom(index); }
const std::vector<RomInfo> &NesViewModel::roms() const noexcept { return library_.roms(); }
const RomInfo *NesViewModel::selected_rom() const noexcept { return library_.selected_rom(); }
std::size_t NesViewModel::selected_index() const noexcept { return library_.selected_index(); }
const std::string &NesViewModel::rom_directory() const noexcept { return library_.rom_directory(); }
const std::string &NesViewModel::states_directory() const noexcept { return help_.states_directory(); }

void NesViewModel::stop_rom() noexcept { game_.stop_rom(); }
bool NesViewModel::save_state() { return game_.save_state(); }
bool NesViewModel::load_state() { return game_.load_state(); }
bool NesViewModel::state_operation_pending() const noexcept { return game_.state_operation_pending(); }
bool NesViewModel::state_status_error() const noexcept { return game_.state_status_error(); }
void NesViewModel::set_button(NesButton button, bool pressed) noexcept { game_.set_button(button, pressed); }
void NesViewModel::set_rapid_button(NesButton button, bool pressed) noexcept { game_.set_rapid_button(button, pressed); }
bool NesViewModel::poll() { return game_.poll(); }
bool NesViewModel::copy_latest_frame(std::uint16_t *destination, std::size_t pixels) noexcept
{
    return game_.copy_latest_frame(destination, pixels);
}

NesKeyMap &NesViewModel::keymap() noexcept { return settings_.keymap(); }
const NesKeyMap &NesViewModel::keymap() const noexcept { return settings_.keymap(); }
NesKeyMapResult NesViewModel::set_key_mapping(NesMappingAction action, std::uint32_t key,
                                             NesKeyMapConflictPolicy policy) noexcept
{
    return settings_.set_key_mapping(action, key, policy);
}
bool NesViewModel::save_keymap(std::error_code &error) const { return settings_.save_keymap(error); }

void NesViewModel::open_browser() { browser_.open_browser(); }
bool NesViewModel::browse_directory(const std::string &path) { return browser_.browse_directory(path); }
void NesViewModel::browse_parent() { browser_.browse_parent(); }
const std::string &NesViewModel::browser_path() const noexcept { return browser_.browser_path(); }
const std::string &NesViewModel::browser_status() const noexcept { return browser_.browser_status(); }
const std::vector<RomFileEntry> &NesViewModel::browser_entries() const noexcept { return browser_.browser_entries(); }

NesUiState NesViewModel::state() const noexcept { return ui_status_.state; }
const std::string &NesViewModel::status() const noexcept { return ui_status_.status; }
std::uint64_t NesViewModel::error_revision() const noexcept { return ui_status_.error_revision; }
