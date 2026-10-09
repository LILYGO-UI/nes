#ifndef LILYGO_UI_NES_PAGES_NES_NES_VIEW_MODEL_HPP
#define LILYGO_UI_NES_PAGES_NES_NES_VIEW_MODEL_HPP

#include "pages/game/game_view_model.hpp"
#include "pages/help/help_view_model.hpp"
#include "pages/library/library_view_model.hpp"
#include "pages/nes/nes_ui_state.hpp"
#include "pages/rom_browser/rom_browser_view_model.hpp"
#include "pages/settings/settings_view_model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

// Application-scoped composition and compatibility API. Commands and page
// state live in the individual page ViewModels below.
class NesViewModel {
public:
    NesViewModel();
    explicit NesViewModel(std::vector<std::string> search_paths);

    [[nodiscard]] LibraryViewModel &library() noexcept;
    [[nodiscard]] const LibraryViewModel &library() const noexcept;
    [[nodiscard]] RomBrowserViewModel &browser() noexcept;
    [[nodiscard]] const RomBrowserViewModel &browser() const noexcept;
    [[nodiscard]] GameViewModel &game() noexcept;
    [[nodiscard]] const GameViewModel &game() const noexcept;
    [[nodiscard]] SettingsViewModel &settings() noexcept;
    [[nodiscard]] const SettingsViewModel &settings() const noexcept;
    [[nodiscard]] HelpViewModel &help() noexcept;
    [[nodiscard]] const HelpViewModel &help() const noexcept;

    void refresh_library();
    bool start_rom(std::size_t index);
    bool import_rom(const std::string &source_path);
    bool delete_rom(std::size_t index);
    void stop_rom() noexcept;
    bool save_state();
    bool load_state();
    [[nodiscard]] bool state_operation_pending() const noexcept;
    [[nodiscard]] bool state_status_error() const noexcept;
    void set_button(NesButton button, bool pressed) noexcept;
    void set_rapid_button(NesButton button, bool pressed) noexcept;
    [[nodiscard]] NesKeyMap &keymap() noexcept;
    [[nodiscard]] const NesKeyMap &keymap() const noexcept;
    NesKeyMapResult set_key_mapping(NesMappingAction action, std::uint32_t key,
                                    NesKeyMapConflictPolicy policy = NesKeyMapConflictPolicy::reject) noexcept;
    bool save_keymap(std::error_code &error) const;
    bool poll();
    void open_browser();
    bool browse_directory(const std::string &path);
    void browse_parent();

    [[nodiscard]] const std::vector<RomInfo> &roms() const noexcept;
    [[nodiscard]] const RomInfo *selected_rom() const noexcept;
    [[nodiscard]] std::size_t selected_index() const noexcept;
    [[nodiscard]] NesUiState state() const noexcept;
    [[nodiscard]] const std::string &status() const noexcept;
    [[nodiscard]] std::uint64_t error_revision() const noexcept;
    [[nodiscard]] const std::string &rom_directory() const noexcept;
    [[nodiscard]] const std::string &states_directory() const noexcept;
    [[nodiscard]] const std::string &browser_path() const noexcept;
    [[nodiscard]] const std::string &browser_status() const noexcept;
    [[nodiscard]] const std::vector<RomFileEntry> &browser_entries() const noexcept;
    bool copy_latest_frame(std::uint16_t *destination, std::size_t pixels) noexcept;

private:
    // Reverse destruction keeps the shared settings and feedback alive until
    // GameViewModel has joined the emulation thread.
    NesUiStatus ui_status_;
    SettingsViewModel settings_;
    GameViewModel game_;
    LibraryViewModel library_;
    RomBrowserViewModel browser_;
    HelpViewModel help_;
};

#endif
