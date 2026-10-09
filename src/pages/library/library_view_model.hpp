#ifndef LILYGO_UI_NES_PAGES_LIBRARY_LIBRARY_VIEW_MODEL_HPP
#define LILYGO_UI_NES_PAGES_LIBRARY_LIBRARY_VIEW_MODEL_HPP

#include "domain/rom_library.hpp"
#include "pages/game/game_view_model.hpp"
#include "pages/nes/nes_ui_state.hpp"

#include <cstddef>
#include <string>
#include <vector>

class LibraryViewModel {
public:
    LibraryViewModel(NesUiStatus &ui_status, GameViewModel &game);
    LibraryViewModel(NesUiStatus &ui_status, GameViewModel &game, std::vector<std::string> search_paths);

    void refresh_library();
    bool start_rom(std::size_t index);
    bool import_rom(const std::string &source_path);
    bool delete_rom(std::size_t index);
    [[nodiscard]] const std::vector<RomInfo> &roms() const noexcept;
    [[nodiscard]] const RomInfo *selected_rom() const noexcept;
    [[nodiscard]] std::size_t selected_index() const noexcept;
    [[nodiscard]] const std::string &rom_directory() const noexcept;
    [[nodiscard]] NesUiState state() const noexcept;
    [[nodiscard]] const std::string &status() const noexcept;
    [[nodiscard]] std::uint64_t error_revision() const noexcept;

private:
    NesUiStatus &ui_status_;
    GameViewModel &game_;
    RomLibrary library_;
    std::size_t selected_index_ = 0;
    std::string rom_directory_;
};

#endif
