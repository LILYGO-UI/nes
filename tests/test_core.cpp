#include "domain/rom_library.hpp"
#include "domain/nes_paths.hpp"

#include <array>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

void write_rom(const std::filesystem::path &path, bool valid)
{
    std::ofstream file(path, std::ios::binary);
    assert(file);
    if (!valid) {
        file << "not a rom";
        return;
    }
    const std::array<unsigned char, 16> header{'N', 'E', 'S', 0x1a, 2, 1, 0x21, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    file.write(reinterpret_cast<const char *>(header.data()), static_cast<std::streamsize>(header.size()));
    std::array<char, 64> payload{};
    file.write(payload.data(), static_cast<std::streamsize>(payload.size()));
}

void test_inspect_and_scan()
{
    const auto directory = std::filesystem::temp_directory_path() /
                           ("lilygo-ui-nes-core-" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::create_directories(directory);
    write_rom(directory / "Zelda_Test.nes", true);
    write_rom(directory / "bad.NES", false);
    std::ofstream(directory / "ignore.txt") << "ignored";

    RomLibrary library({directory.string()});
    library.scan();
    assert(library.roms().size() == 2);
    assert(library.roms()[0].title == "bad");
    assert(!library.roms()[0].valid_header);
    assert(library.roms()[1].title == "Zelda Test");
    assert(library.roms()[1].valid_header);
    assert(library.roms()[1].mapper == 2);
    assert(library.roms()[1].prg_banks == 2);
    assert(library.roms()[1].chr_banks == 1);
    assert(library.roms()[1].crc32 != 0);
    assert(library.roms()[1].metadata.find("PAL") != std::string::npos);
    assert(library.primary_path() == directory.string());

    std::error_code error;
    assert(!library.delete_rom(2, error));
    assert(error == std::errc::invalid_argument);
    assert(library.roms().size() == 2);
    assert(library.delete_rom(0, error));
    assert(!error);
    assert(!std::filesystem::exists(directory / "bad.NES"));
    assert(library.roms().size() == 1);
    assert(library.roms()[0].title == "Zelda Test");

    std::filesystem::remove(directory / "Zelda_Test.nes");
    std::filesystem::create_directory(directory / "Zelda_Test.nes");
    assert(!library.delete_rom(0, error));
    assert(error);
    assert(std::filesystem::is_directory(directory / "Zelda_Test.nes"));
    assert(library.roms().size() == 1);

    std::filesystem::remove_all(directory);
}

void test_scan_across_paths_and_library_limit()
{
    const auto directory = std::filesystem::temp_directory_path() /
                           ("lilygo-ui-nes-paths-" + std::to_string(static_cast<long long>(getpid())));
    const auto first   = directory / "first";
    const auto second  = directory / "second";
    const auto empty   = directory / "empty";
    const auto missing = directory / "missing";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);
    std::filesystem::create_directories(empty);
    const auto first_count = RomLibrary::maximum_roms + 3;
    for (std::size_t index = 0; index < first_count; ++index) {
        write_rom(first / (std::to_string(index) + ".nes"), true);
    }
    write_rom(second / "valid.NES", true);
    write_rom(second / "invalid.nes", false);
    std::ofstream(second / "ignore.txt") << "ignored";
    std::filesystem::create_directory(second / "folder.nes");
    write_rom(second / "folder.nes" / "nested.nes", true);

    const std::vector<std::string> paths{first.string(), missing.string(), second.string(), empty.string()};
    RomLibrary library(paths);
    library.scan();
    assert(library.roms().size() == RomLibrary::maximum_roms);
    assert(library.search_paths() == paths);
    for (const auto &rom : library.roms()) {
        assert(std::filesystem::path(rom.path).parent_path() == first);
    }

    std::error_code error;
    assert(library.delete_rom(0, error));
    assert(!error);
    assert(library.roms().size() == RomLibrary::maximum_roms);

    std::filesystem::remove_all(first);
    library.scan();
    assert(library.roms().size() == 2);
    assert(library.roms()[0].title == "invalid");
    assert(library.roms()[1].title == "valid");

    std::filesystem::remove_all(second);
    std::filesystem::create_directory(missing);
    write_rom(missing / "new.nes", true);
    library.scan();
    assert(library.roms().size() == 1);
    assert(library.roms()[0].path == (missing / "new.nes").string());
    std::filesystem::remove_all(directory);
}

void test_default_directory()
{
    const char *original_home_value = std::getenv("HOME");
    const bool had_home = original_home_value != nullptr;
    const std::string original_home = original_home_value ? original_home_value : "";
    const char *original_override_value = std::getenv("NES_ROM_DIR");
    const bool had_override = original_override_value != nullptr;
    const std::string original_override = original_override_value ? original_override_value : "";
    const auto directory = std::filesystem::temp_directory_path() /
                           ("lilygo-ui-nes-home-" + std::to_string(static_cast<long long>(getpid())));
    const auto home = directory / "home";
    const auto other = directory / "other";
    std::filesystem::create_directories(home / "nes" / "rom");
    std::filesystem::create_directories(home / "nes" / "states");
    std::filesystem::create_directories(other);
    write_rom(home / "nes" / "rom" / "stored.nes", true);
    write_rom(home / "nes" / "old-location.nes", true);
    write_rom(home / "nes" / "states" / "ignored.nes", true);
    write_rom(other / "ignored.nes", true);
    assert(setenv("HOME", home.c_str(), 1) == 0);
    assert(setenv("NES_ROM_DIR", other.c_str(), 1) == 0);
    {
        RomLibrary library;
        assert(library.search_paths() == std::vector<std::string>{(home / "nes" / "rom").string()});
        library.scan();
        assert(library.roms().size() == 1);
        assert(library.roms()[0].title == "stored");
        const auto stored_rom = library.roms()[0].path;
        const auto states = home / "nes" / "states";
        assert(nes_paths::states_directory() == states.string());
        assert(nes_paths::state_path(stored_rom) == (states / "stored.nes.state").string());
        assert(nes_paths::battery_path(stored_rom) == (states / "stored.nes.sav").string());
        assert(nes_paths::state_path((other / "stored.nes").string()) == nes_paths::state_path(stored_rom));
        assert(nes_paths::state_path("stored.NES") != nes_paths::state_path(stored_rom));
        assert(nes_paths::state_path("").empty());
        // A valid maximum-length ROM name must still have writable save names.
        const std::string long_name = std::string(251, 'x') + ".nes";
        const auto long_rom = home / "nes" / "rom" / long_name;
        write_rom(long_rom, true);
        const auto long_state = std::filesystem::path(nes_paths::state_path(long_rom.string()));
        const auto long_battery = std::filesystem::path(nes_paths::battery_path(long_rom.string()));
        assert(long_state.parent_path() == states && long_battery.parent_path() == states);
        assert(long_state.filename().string().size() <= 255);
        assert(long_battery.filename().string().size() <= 255);
        assert(long_state.stem() == long_battery.stem());
        assert(long_state == nes_paths::state_path(long_rom.string()));
        assert(long_state != nes_paths::state_path(std::string(250, 'x') + "y.nes"));
        std::ofstream(long_state) << "state";
        std::ofstream(long_battery) << "battery";
        assert(std::filesystem::file_size(long_state) == 5);
        assert(std::filesystem::file_size(long_battery) == 7);
        // Removing a game leaves both kinds of progress available for reimport.
        std::ofstream(nes_paths::state_path(stored_rom)) << "state";
        std::ofstream(nes_paths::battery_path(stored_rom)) << "battery";
        std::error_code error;
        assert(library.delete_rom(0, error));
        assert(std::filesystem::exists(nes_paths::state_path(stored_rom)));
        assert(std::filesystem::exists(nes_paths::battery_path(stored_rom)));
    }
    assert(setenv("HOME", "", 1) == 0);
    assert(RomLibrary::default_search_paths().empty());
    assert(nes_paths::states_directory().empty());
    assert(nes_paths::state_path("game.nes").empty());
    assert(unsetenv("HOME") == 0);
    assert(RomLibrary::default_search_paths().empty());
    assert(nes_paths::battery_path("game.nes").empty());
    assert(setenv("HOME", "relative-home", 1) == 0);
    assert(RomLibrary::default_search_paths().empty());
    assert(nes_paths::states_directory().empty());
    if (had_home) assert(setenv("HOME", original_home.c_str(), 1) == 0);
    else assert(unsetenv("HOME") == 0);
    if (had_override) assert(setenv("NES_ROM_DIR", original_override.c_str(), 1) == 0);
    else assert(unsetenv("NES_ROM_DIR") == 0);
    std::filesystem::remove_all(directory);
}

void test_import_rom()
{
    const auto directory = std::filesystem::temp_directory_path() /
                           ("lilygo-ui-nes-import-" + std::to_string(static_cast<long long>(getpid())));
    const auto source = directory / "source";
    const auto destination = directory / "home" / "nes" / "rom";
    std::filesystem::create_directories(source);
    write_rom(source / "added.NES", true);
    write_rom(source / "invalid.nes", false);
    write_rom(source / "wrong.txt", true);
    std::filesystem::create_directory(source / "folder.nes");
    RomLibrary library({destination.string()});
    std::error_code error;
    assert(!library.import_rom((source / "invalid.nes").string(), error));
    assert(error == std::errc::illegal_byte_sequence);
    assert(!std::filesystem::exists(destination));
    assert(!library.import_rom((source / "wrong.txt").string(), error));
    assert(error == std::errc::invalid_argument);
    assert(!library.import_rom((source / "missing.nes").string(), error));
    assert(error);
    assert(!library.import_rom((source / "folder.nes").string(), error));
    assert(error == std::errc::invalid_argument);

    const auto source_info = RomLibrary::inspect((source / "added.NES").string());
    assert(library.import_rom((source / "added.NES").string(), error));
    assert(!error);
    assert(std::filesystem::exists(source / "added.NES"));
    assert(std::filesystem::exists(destination / "added.NES"));
    assert(library.roms().size() == 1);
    assert(library.roms()[0].crc32 == source_info.crc32);

    std::ofstream(source / "added.NES", std::ios::binary | std::ios::app) << "changed";
    assert(!library.import_rom((source / "added.NES").string(), error));
    assert(error == std::errc::file_exists);
    assert(RomLibrary::inspect((destination / "added.NES").string()).crc32 == source_info.crc32);
    assert(!library.import_rom((destination / "added.NES").string(), error));
    assert(error == std::errc::file_exists);
    assert(library.roms().size() == 1);

    for (std::size_t index = 1; index < RomLibrary::maximum_roms; ++index) {
        write_rom(destination / ("existing-" + std::to_string(index) + ".nes"), true);
    }
    write_rom(source / "overflow.nes", true);
    assert(!library.import_rom((source / "overflow.nes").string(), error));
    assert(error == std::errc::value_too_large);
    assert(!std::filesystem::exists(destination / "overflow.nes"));
    assert(std::filesystem::exists(source / "overflow.nes"));
    std::filesystem::remove(destination / "existing-1.nes");
    assert(library.import_rom((source / "overflow.nes").string(), error));
    assert(!error);
    assert(library.roms().size() == RomLibrary::maximum_roms);

    RomLibrary unavailable(std::vector<std::string>{});
    assert(!unavailable.import_rom((source / "added.NES").string(), error));
    assert(error == std::errc::no_such_file_or_directory);
    const auto blocked_directory = directory / "not-a-directory";
    std::ofstream(blocked_directory) << "occupied";
    RomLibrary blocked({blocked_directory.string()});
    assert(!blocked.import_rom((source / "added.NES").string(), error));
    assert(error);
    assert(std::filesystem::is_regular_file(blocked_directory));
    std::filesystem::remove_all(directory);
}

void test_browse_directory()
{
    const auto directory = std::filesystem::temp_directory_path() /
                           ("lilygo-ui-nes-browse-" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::create_directories(directory / "z-folder");
    std::filesystem::create_directory(directory / "A-folder");
    write_rom(directory / "z-rom.NES", true);
    write_rom(directory / "a-rom.nes", false);
    std::ofstream(directory / "ignored.txt") << "ignored";
    std::filesystem::create_symlink(directory / "missing", directory / "dangling.nes");
    std::filesystem::create_symlink(directory / "loop.nes", directory / "loop.nes");
    std::error_code error;
    const auto entries = RomLibrary::browse_directory(directory.string(), error);
    assert(!error);
    assert(entries.size() == 4);
    assert(entries[0].name == "A-folder" && entries[0].directory);
    assert(entries[1].name == "z-folder" && entries[1].directory);
    assert(entries[2].name == "a-rom.nes" && !entries[2].directory);
    assert(entries[3].name == "z-rom.NES" && !entries[3].directory);
    assert(entries[2].path == (directory / "a-rom.nes").string());
    assert(RomLibrary::browse_directory((directory / "missing").string(), error).empty());
    assert(error);
    assert(RomLibrary::browse_directory((directory / "a-rom.nes").string(), error).empty());
    assert(error == std::errc::not_a_directory);
    assert(RomLibrary::browse_directory("relative", error).empty());
    assert(error == std::errc::invalid_argument);
    std::filesystem::remove_all(directory);
}

}  // namespace

int main()
{
    test_inspect_and_scan();
    test_scan_across_paths_and_library_limit();
    test_default_directory();
    test_import_rom();
    test_browse_directory();
    return 0;
}
