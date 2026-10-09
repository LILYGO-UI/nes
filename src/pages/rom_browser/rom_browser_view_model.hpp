#ifndef LILYGO_UI_NES_PAGES_ROM_BROWSER_ROM_BROWSER_VIEW_MODEL_HPP
#define LILYGO_UI_NES_PAGES_ROM_BROWSER_ROM_BROWSER_VIEW_MODEL_HPP

#include "domain/rom_library.hpp"

#include <string>
#include <vector>

class LibraryViewModel;

class RomBrowserViewModel {
public:
    explicit RomBrowserViewModel(LibraryViewModel &library) noexcept;

    void open_browser();
    bool browse_directory(const std::string &path);
    void browse_parent();
    bool import_rom(const std::string &source_path);
    [[nodiscard]] const std::string &browser_path() const noexcept;
    [[nodiscard]] const std::string &browser_status() const noexcept;
    [[nodiscard]] const std::vector<RomFileEntry> &browser_entries() const noexcept;

private:
    LibraryViewModel &library_;
    std::string browser_path_;
    std::string browser_status_;
    std::vector<RomFileEntry> browser_entries_;
};

#endif
