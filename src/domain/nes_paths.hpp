#ifndef LILYGO_UI_NES_DOMAIN_NES_PATHS_HPP
#define LILYGO_UI_NES_DOMAIN_NES_PATHS_HPP

#include <string>

namespace nes_paths {

// An unset, empty or relative HOME returns an empty path. Never fall back to
// the working directory when the launching user's storage is unavailable.
[[nodiscard]] std::string rom_directory();
[[nodiscard]] std::string states_directory();
// Keep the complete ROM filename so game.nes and game.NES have separate saves.
// Names too long for a save extension use a shortened prefix and stable hash.
[[nodiscard]] std::string state_path(const std::string &rom_path);
[[nodiscard]] std::string battery_path(const std::string &rom_path);

}  // namespace nes_paths

#endif
