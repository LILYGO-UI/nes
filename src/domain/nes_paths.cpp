#include "domain/nes_paths.hpp"

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace {

std::string directory(const char *name)
{
    const char *home = std::getenv("HOME");
    if (!home || !*home || !std::filesystem::path(home).is_absolute()) return {};
    return (std::filesystem::path(home) / "nes" / name).string();
}

std::string save_path(const std::string &rom_path, const char *extension)
{
    const auto storage = nes_paths::states_directory();
    const auto filename = std::filesystem::path(rom_path).filename();
    if (storage.empty() || filename.empty() || filename == "." || filename == "..") return {};
    auto name = filename.string();
    // Reserve six bytes for .state on filesystems with 255-byte components.
    // Hash the whole filename so long ROM names with the same prefix do not
    // overwrite one another. Both kinds of save use the same shortened stem.
    if (name.size() > 249) {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const unsigned char byte : name) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        std::size_t prefix_size = 230;
        while (prefix_size > 0 && (static_cast<unsigned char>(name[prefix_size]) & 0xc0U) == 0x80U) {
            --prefix_size;
        }
        std::ostringstream suffix;
        suffix << '~' << std::hex << std::setfill('0') << std::setw(16) << hash;
        name = name.substr(0, prefix_size) + suffix.str();
    }
    return (std::filesystem::path(storage) / (name + extension)).string();
}

}  // namespace

namespace nes_paths {

std::string rom_directory()
{
    return directory("rom");
}

std::string states_directory()
{
    return directory("states");
}

std::string state_path(const std::string &rom_path)
{
    return save_path(rom_path, ".state");
}

std::string battery_path(const std::string &rom_path)
{
    return save_path(rom_path, ".sav");
}

}  // namespace nes_paths
