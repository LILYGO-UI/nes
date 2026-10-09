#ifndef LILYGO_UI_NES_DOMAIN_ROM_LIBRARY_HPP
#define LILYGO_UI_NES_DOMAIN_ROM_LIBRARY_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

struct RomInfo {
    std::string path;
    std::string title;
    std::string metadata;
    std::uintmax_t size    = 0;
    std::uint32_t crc32    = 0;
    std::uint8_t mapper    = 0;
    std::uint8_t prg_banks = 0;
    std::uint8_t chr_banks = 0;
    bool valid_header      = false;
};

struct RomFileEntry {
    std::string path;
    std::string name;
    bool directory = false;
};

class RomLibrary {
public:
    static constexpr std::size_t maximum_roms = 128;

    RomLibrary();
    explicit RomLibrary(std::vector<std::string> search_paths);

    void scan();
    bool import_rom(const std::string &source_path, std::error_code &error);
    bool delete_rom(std::size_t index, std::error_code &error);

    [[nodiscard]] const std::vector<RomInfo> &roms() const noexcept;
    [[nodiscard]] const std::vector<std::string> &search_paths() const noexcept;
    [[nodiscard]] std::string primary_path() const;

    [[nodiscard]] static RomInfo inspect(const std::string &path);
    [[nodiscard]] static std::vector<std::string> default_search_paths();
    [[nodiscard]] static std::vector<RomFileEntry> browse_directory(const std::string &path,
                                                                   std::error_code &error);

private:
    std::vector<std::string> search_paths_;
    std::vector<RomInfo> roms_;
};

#endif
