#ifndef LILYGO_UI_NES_PAGES_HELP_HELP_VIEW_MODEL_HPP
#define LILYGO_UI_NES_PAGES_HELP_HELP_VIEW_MODEL_HPP

#include <string>

class HelpViewModel {
public:
    HelpViewModel(std::string rom_directory, std::string states_directory);

    [[nodiscard]] const std::string &rom_directory() const noexcept;
    [[nodiscard]] const std::string &states_directory() const noexcept;

private:
    std::string rom_directory_;
    std::string states_directory_;
};

#endif
