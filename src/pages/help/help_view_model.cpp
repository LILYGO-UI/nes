#include "pages/help/help_view_model.hpp"

#include <utility>

HelpViewModel::HelpViewModel(std::string rom_directory, std::string states_directory)
    : rom_directory_(std::move(rom_directory)), states_directory_(std::move(states_directory))
{
}

const std::string &HelpViewModel::rom_directory() const noexcept
{
    return rom_directory_;
}

const std::string &HelpViewModel::states_directory() const noexcept
{
    return states_directory_;
}
