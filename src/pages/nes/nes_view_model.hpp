#ifndef LILYGO_UI_NES_PAGES_NES_NES_VIEW_MODEL_HPP
#define LILYGO_UI_NES_PAGES_NES_NES_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/rom_library.hpp"
#include "emulator/nes_emulator.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class NesUiState {
    library,
    running,
    stopping,
    error,
};

class NesViewModel {
public:
    NesViewModel();
    explicit NesViewModel(std::vector<std::string> search_paths);

    void refresh_library();
    bool start_rom(std::size_t index);
    bool delete_rom(std::size_t index);
    void stop_rom() noexcept;
    void set_button(NesButton button, bool pressed) noexcept;
    bool poll();

    [[nodiscard]] const std::vector<RomInfo> &roms() const noexcept;
    [[nodiscard]] const RomInfo *selected_rom() const noexcept;
    [[nodiscard]] std::size_t selected_index() const noexcept;
    [[nodiscard]] NesUiState state() const noexcept;
    [[nodiscard]] const std::string &status() const noexcept;
    [[nodiscard]] const std::string &rom_directory() const noexcept;
    [[nodiscard]] lv_subject_t *rom_count_subject() noexcept;
    bool copy_latest_frame(std::uint16_t *destination, std::size_t pixels) noexcept;

private:
    void initialize();

    RomLibrary library_;
    NesEmulator emulator_;
    IntSubject rom_count_{0, 0, static_cast<std::int32_t>(RomLibrary::maximum_roms)};
    std::size_t selected_index_ = 0;
    NesUiState state_           = NesUiState::library;
    std::string status_;
    std::string rom_directory_;
};

#endif
