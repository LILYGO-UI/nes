#include "domain/rom_library.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace {

std::string display_title(const std::filesystem::path &path)
{
    auto title = path.stem().string();
    std::replace(title.begin(), title.end(), '_', ' ');
    std::replace(title.begin(), title.end(), '-', ' ');
    return title.empty() ? path.filename().string() : title;
}

bool has_nes_extension(const std::filesystem::path &path)
{
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".nes";
}

std::uint32_t crc32_file(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return 0;

    std::uint32_t crc = 0xffffffffU;
    std::array<char, 4096> buffer{};
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = file.gcount();
        for (std::streamsize index = 0; index < count; ++index) {
            crc ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc >> 1U) ^ ((crc & 1U) ? 0xedb88320U : 0U);
            }
        }
    }
    return ~crc;
}

void add_unique(std::vector<std::string> &paths, const std::string &path)
{
    if (!path.empty() && std::find(paths.cbegin(), paths.cend(), path) == paths.cend()) paths.push_back(path);
}

bool title_less(const std::string &left, const std::string &right)
{
    return std::lexicographical_compare(left.cbegin(), left.cend(), right.cbegin(), right.cend(),
                                        [](unsigned char left_value, unsigned char right_value) {
                                            return std::tolower(left_value) < std::tolower(right_value);
                                        });
}

}  // namespace

RomLibrary::RomLibrary() : RomLibrary(default_search_paths())
{
}

RomLibrary::RomLibrary(std::vector<std::string> search_paths) : search_paths_(std::move(search_paths))
{
}

void RomLibrary::scan()
{
    roms_.clear();
    std::error_code error;

    for (const auto &search_path : search_paths_) {
        const std::filesystem::path directory(search_path);
        if (!std::filesystem::is_directory(directory, error)) {
            error.clear();
            continue;
        }

        for (std::filesystem::directory_iterator iterator(directory, error), end; iterator != end && !error;
             iterator.increment(error)) {
            if (roms_.size() >= maximum_roms) break;
            if (!iterator->is_regular_file(error) || !has_nes_extension(iterator->path())) {
                error.clear();
                continue;
            }
            roms_.push_back(inspect(iterator->path().string()));
        }
        error.clear();
        if (roms_.size() >= maximum_roms) break;
    }

    std::sort(roms_.begin(), roms_.end(),
              [](const RomInfo &left, const RomInfo &right) { return title_less(left.title, right.title); });
}

bool RomLibrary::delete_rom(std::size_t index, std::error_code &error)
{
    error.clear();
    if (index >= roms_.size()) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }

    const auto path = roms_[index].path;
    if (!std::filesystem::is_regular_file(path, error)) {
        if (!error) error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    if (!std::filesystem::remove(path, error)) {
        if (!error) error = std::make_error_code(std::errc::no_such_file_or_directory);
        return false;
    }

    scan();
    return true;
}

const std::vector<RomInfo> &RomLibrary::roms() const noexcept
{
    return roms_;
}

const std::vector<std::string> &RomLibrary::search_paths() const noexcept
{
    return search_paths_;
}

std::string RomLibrary::primary_path() const
{
    return search_paths_.empty() ? std::string{} : search_paths_.front();
}

RomInfo RomLibrary::inspect(const std::string &path)
{
    RomInfo result;
    result.path  = path;
    result.title = display_title(std::filesystem::path(path));

    std::error_code error;
    result.size = std::filesystem::file_size(path, error);
    if (error) result.size = 0;

    std::array<unsigned char, 16> header{};
    std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char *>(header.data()), static_cast<std::streamsize>(header.size()));
    result.valid_header = file.gcount() == static_cast<std::streamsize>(header.size()) && header[0] == 'N' &&
                          header[1] == 'E' && header[2] == 'S' && header[3] == 0x1a;

    if (!result.valid_header) {
        result.metadata = "Invalid iNES header";
        return result;
    }

    result.prg_banks       = header[4];
    result.chr_banks       = header[5];
    result.mapper          = static_cast<std::uint8_t>((header[6] >> 4U) | (header[7] & 0xf0U));
    const bool vertical    = (header[6] & 0x01U) != 0;
    const bool battery     = (header[6] & 0x02U) != 0;
    const bool trainer     = (header[6] & 0x04U) != 0;
    const bool four_screen = (header[6] & 0x08U) != 0;
    const bool pal         = (header[9] & 0x01U) != 0;

    std::ostringstream metadata;
    metadata << "Mapper " << static_cast<unsigned>(result.mapper) << "  PRG "
             << static_cast<unsigned>(result.prg_banks) * 16U << "KB  CHR "
             << static_cast<unsigned>(result.chr_banks) * 8U << "KB  " << (pal ? "PAL" : "NTSC") << "  "
             << (four_screen ? "4-screen" : (vertical ? "Vertical" : "Horizontal"));
    if (battery) metadata << "  Battery";
    if (trainer) metadata << "  Trainer";
    result.metadata = metadata.str();
    result.crc32    = crc32_file(path);
    return result;
}

std::vector<std::string> RomLibrary::default_search_paths()
{
    std::vector<std::string> paths;
    if (const char *override_path = std::getenv("NES_ROM_DIR")) add_unique(paths, override_path);
    if (const char *home = std::getenv("HOME")) add_unique(paths, std::string(home) + "/nes");
    add_unique(paths, "/root/nes");
    add_unique(paths, "/root/roms/nes");
    add_unique(paths, "/mnt/sdcard/nes");
    add_unique(paths, "/mnt/nes");
    add_unique(paths, "/media/nes");
    return paths;
}
