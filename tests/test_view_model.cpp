#include "pages/nes/nes_view_model.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

void write_running_rom(const std::filesystem::path &path)
{
    std::vector<unsigned char> bytes(16 + 16384 + 8192, 0);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1a;
    bytes[4] = 1;
    bytes[5] = 1;
    bytes[16] = 0x4c;
    bytes[18] = 0x80;
    bytes[16 + 0x3ffb] = 0x80;
    bytes[16 + 0x3ffd] = 0x80;
    bytes[16 + 0x3fff] = 0x80;
    std::ofstream file(path, std::ios::binary);
    assert(file);
    file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

int main()
{
    const auto directory = std::filesystem::temp_directory_path() /
                           ("lilygo-ui-nes-vm-" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "broken.nes", std::ios::binary) << "broken";

    lv_init();
    {
        NesViewModel view_model({directory.string()});
        assert(view_model.roms().size() == 1);
        assert(lv_subject_get_int(view_model.rom_count_subject()) == 1);
        assert(view_model.selected_rom());
        assert(!view_model.start_rom(0));
        assert(view_model.state() == NesUiState::error);
        assert(view_model.status() == "Invalid iNES ROM");
        assert(!view_model.start_rom(4));
        view_model.refresh_library();
        assert(view_model.state() == NesUiState::library);

        assert(!view_model.delete_rom(4));
        assert(view_model.state() == NesUiState::error);
        assert(view_model.status().find("Could not delete ROM:") == 0);
        assert(view_model.roms().size() == 1);
        assert(lv_subject_get_int(view_model.rom_count_subject()) == 1);

        assert(view_model.delete_rom(0));
        assert(!std::filesystem::exists(directory / "broken.nes"));
        assert(view_model.roms().empty());
        assert(lv_subject_get_int(view_model.rom_count_subject()) == 0);
        assert(!view_model.selected_rom());
        assert(view_model.state() == NesUiState::library);
        assert(view_model.status() == "No ROM files found");
        view_model.refresh_library();
        assert(view_model.roms().empty());

        std::ofstream(directory / "unavailable.nes", std::ios::binary) << "broken";
        view_model.refresh_library();
        std::filesystem::remove(directory / "unavailable.nes");
        std::filesystem::create_directory(directory / "unavailable.nes");
        assert(!view_model.delete_rom(0));
        assert(view_model.state() == NesUiState::error);
        assert(view_model.status().find("Could not delete ROM:") == 0);
        assert(view_model.roms().size() == 1);
        assert(lv_subject_get_int(view_model.rom_count_subject()) == 1);
        assert(std::filesystem::is_directory(directory / "unavailable.nes"));
    }
    {
        write_running_rom(directory / "running.nes");
        assert(setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
        NesViewModel view_model({directory.string()});
        assert(view_model.start_rom(0));
        assert(!view_model.delete_rom(0));
        assert(view_model.state() == NesUiState::running);
        assert(view_model.status() == "Close the game before deleting ROMs");
        assert(std::filesystem::exists(directory / "running.nes"));
        assert(view_model.roms().size() == 1);
        view_model.stop_rom();
        assert(!view_model.delete_rom(0));
        assert(view_model.state() == NesUiState::stopping);
        assert(std::filesystem::exists(directory / "running.nes"));
    }
    lv_deinit();
    std::filesystem::remove_all(directory);
    return 0;
}
