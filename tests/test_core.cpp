#include "domain/rom_library.hpp"

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

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

}  // namespace

int main()
{
    test_inspect_and_scan();
    return 0;
}
