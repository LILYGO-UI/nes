#include "pages/rom_browser/rom_browser_view_model.hpp"

#include "pages/library/library_view_model.hpp"

#include <cstdlib>
#include <filesystem>
#include <utility>

RomBrowserViewModel::RomBrowserViewModel(LibraryViewModel &library) noexcept : library_(library)
{
}

void RomBrowserViewModel::open_browser()
{
    const char *home = std::getenv("HOME");
    if (home && *home && std::filesystem::path(home).is_absolute() && browse_directory(home)) return;
    browse_directory("/");
}

bool RomBrowserViewModel::browse_directory(const std::string &path)
{
    std::error_code error;
    auto entries = RomLibrary::browse_directory(path, error);
    if (error) {
        browser_status_ = "Could not open folder: " + error.message();
        return false;
    }
    auto directory = std::filesystem::path(path).lexically_normal();
    if (directory.has_relative_path() && directory.filename().empty()) directory = directory.parent_path();
    browser_path_ = directory.string();
    browser_entries_ = std::move(entries);
    browser_status_.clear();
    return true;
}

void RomBrowserViewModel::browse_parent()
{
    if (browser_path_.empty()) {
        open_browser();
        return;
    }
    browse_directory(std::filesystem::path(browser_path_).parent_path().string());
}

bool RomBrowserViewModel::import_rom(const std::string &source_path)
{
    return library_.import_rom(source_path);
}

const std::string &RomBrowserViewModel::browser_path() const noexcept
{
    return browser_path_;
}

const std::string &RomBrowserViewModel::browser_status() const noexcept
{
    return browser_status_;
}

const std::vector<RomFileEntry> &RomBrowserViewModel::browser_entries() const noexcept
{
    return browser_entries_;
}
