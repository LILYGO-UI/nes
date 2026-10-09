#include "emulator/nes_input_mapping.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>

namespace {

constexpr std::array<std::uint32_t, nes_mapping_action_count> kDefaultKeys{
    static_cast<std::uint32_t>('w'), static_cast<std::uint32_t>('s'), static_cast<std::uint32_t>('a'),
    static_cast<std::uint32_t>('d'), static_cast<std::uint32_t>('k'), static_cast<std::uint32_t>('j'),
    static_cast<std::uint32_t>('i'), static_cast<std::uint32_t>('u'), static_cast<std::uint32_t>('1'),
    static_cast<std::uint32_t>('2'),
};

std::string_view trim(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    return value;
}

std::optional<NesMappingAction> action_from_name(std::string_view name)
{
    for (std::size_t index = 0; index < nes_mapping_action_count; ++index) {
        const auto action = static_cast<NesMappingAction>(index);
        if (name == nes_mapping_action_name(action)) return action;
    }
    return std::nullopt;
}

std::error_code parse_error()
{
    return std::make_error_code(std::errc::invalid_argument);
}

std::filesystem::path resolved_path(const std::filesystem::path &path)
{
    return path.empty() ? NesKeyMap::config_path() : path;
}

}  // namespace

NesKeyMap::NesKeyMap() noexcept : keys_(kDefaultKeys)
{
}

NesKeyMap NesKeyMap::defaults() noexcept
{
    return NesKeyMap{};
}

std::filesystem::path NesKeyMap::config_path()
{
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        const std::filesystem::path configured(xdg);
        if (configured.is_absolute()) return configured / "lilygo-ui-nes" / "keymap.conf";
    }

    const char *home = std::getenv("HOME");
    if (home && *home) {
        const std::filesystem::path configured(home);
        if (configured.is_absolute()) return configured / ".config" / "lilygo-ui-nes" / "keymap.conf";
    }
    return {};
}

std::uint32_t NesKeyMap::key(NesMappingAction action) const noexcept
{
    if (!nes_valid_mapping_action(action)) return 0;
    return keys_[static_cast<std::size_t>(action)];
}

const std::array<std::uint32_t, nes_mapping_action_count> &NesKeyMap::keys() const noexcept
{
    return keys_;
}

NesKeyMapResult NesKeyMap::set_key(NesMappingAction action, std::uint32_t value,
                                   NesKeyMapConflictPolicy policy) noexcept
{
    if (!nes_valid_mapping_action(action)) return NesKeyMapResult::invalid_action;
    if (!nes_valid_key(value)) return NesKeyMapResult::invalid_key;

    const auto normalized = nes_normalize_key(value);
    const auto index      = static_cast<std::size_t>(action);
    std::size_t owner     = nes_mapping_action_count;
    for (std::size_t candidate = 0; candidate < nes_mapping_action_count; ++candidate) {
        if (candidate != index && keys_[candidate] == normalized) {
            owner = candidate;
            break;
        }
    }

    if (owner != nes_mapping_action_count) {
        if (policy == NesKeyMapConflictPolicy::reject) return NesKeyMapResult::conflict;
        std::swap(keys_[index], keys_[owner]);
        return NesKeyMapResult::applied;
    }

    keys_[index] = normalized;
    return NesKeyMapResult::applied;
}

bool NesKeyMap::clear_key(NesMappingAction action) noexcept
{
    if (!nes_valid_mapping_action(action)) return false;
    keys_[static_cast<std::size_t>(action)] = 0;
    return true;
}

bool NesKeyMap::has_key(std::uint32_t value, NesMappingAction ignored) const noexcept
{
    if (!nes_valid_key(value)) return false;
    const auto normalized = nes_normalize_key(value);
    for (std::size_t index = 0; index < nes_mapping_action_count; ++index) {
        if (index == static_cast<std::size_t>(ignored)) continue;
        if (keys_[index] == normalized) return true;
    }
    return false;
}

std::optional<NesMappingAction> NesKeyMap::action_for(std::uint32_t value) const noexcept
{
    if (!nes_valid_key(value)) return std::nullopt;
    const auto normalized = nes_normalize_key(value);
    for (std::size_t index = 0; index < nes_mapping_action_count; ++index) {
        if (keys_[index] == normalized) return static_cast<NesMappingAction>(index);
    }
    return std::nullopt;
}

NesKeyBinding NesKeyMap::binding(std::uint32_t value) const noexcept
{
    const auto action = action_for(value);
    if (!action) return {};
    return {nes_mapping_button(*action), nes_mapping_is_rapid(*action), true};
}

bool NesKeyMap::load(const std::filesystem::path &requested_path, std::error_code &error)
{
    error.clear();
    const auto path = resolved_path(requested_path);
    if (path.empty()) {
        error = std::make_error_code(std::errc::no_such_file_or_directory);
        return false;
    }

    std::ifstream file(path);
    if (!file) {
        error = std::make_error_code(std::errc::no_such_file_or_directory);
        return false;
    }

    NesKeyMap candidate = defaults();
    std::array<bool, nes_mapping_action_count> seen{};
    std::array<std::uint32_t, nes_mapping_action_count> parsed_keys{};
    std::string line;
    while (std::getline(file, line)) {
        auto text = trim(line);
        if (text.empty() || text.front() == '#') continue;
        const auto equals = text.find('=');
        if (equals == std::string_view::npos) {
            error = parse_error();
            return false;
        }
        const auto name  = trim(text.substr(0, equals));
        const auto value = trim(text.substr(equals + 1));
        const auto action = action_from_name(name);
        // Unknown fields are ignored to permit future versions to add options.
        if (!action) continue;
        const auto index = static_cast<std::size_t>(*action);
        if (seen[index]) {
            error = parse_error();
            return false;
        }
        seen[index] = true;

        std::uint64_t parsed = 0;
        const auto *begin    = value.data();
        const auto *end      = begin + value.size();
        const auto result    = std::from_chars(begin, end, parsed, 10);
        if (result.ec != std::errc{} || result.ptr != end || parsed > std::numeric_limits<std::uint32_t>::max()) {
            error = parse_error();
            return false;
        }
        const auto key_code = static_cast<std::uint32_t>(parsed);
        if (!nes_valid_key(key_code)) {
            if (key_code == 0) {
                parsed_keys[index] = 0;
                continue;
            }
            error = parse_error();
            return false;
        }
        parsed_keys[index] = key_code;
    }
    if (file.bad()) {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }

    // Clear all explicitly supplied actions before assigning their values so
    // a valid remap does not depend on the order of lines in the file.
    for (std::size_t index = 0; index < nes_mapping_action_count; ++index) {
        if (seen[index]) candidate.clear_key(static_cast<NesMappingAction>(index));
    }
    for (std::size_t index = 0; index < nes_mapping_action_count; ++index) {
        if (!seen[index] || parsed_keys[index] == 0) continue;
        const auto action = static_cast<NesMappingAction>(index);
        const auto status = candidate.set_key(action, parsed_keys[index], NesKeyMapConflictPolicy::reject);
        if (status != NesKeyMapResult::applied) {
            error = parse_error();
            return false;
        }
    }

    keys_ = candidate.keys_;
    return true;
}

bool NesKeyMap::save(const std::filesystem::path &requested_path, std::error_code &error) const
{
    error.clear();
    const auto path = resolved_path(requested_path);
    if (path.empty()) {
        error = std::make_error_code(std::errc::no_such_file_or_directory);
        return false;
    }

    auto parent = path.parent_path();
    if (parent.empty()) parent = ".";
    std::filesystem::create_directories(parent, error);
    if (error) return false;

    auto temporary = path;
    temporary += ".tmp-" +
                 std::to_string(static_cast<unsigned long long>(
                     std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file) {
            error = std::make_error_code(std::errc::permission_denied);
            return false;
        }
        file << "# LilyGo UI NES keymap v1\n";
        for (std::size_t index = 0; index < nes_mapping_action_count; ++index) {
            file << nes_mapping_action_name(static_cast<NesMappingAction>(index)) << '=' << keys_[index] << '\n';
        }
        file.flush();
        if (!file) {
            error = std::make_error_code(std::errc::io_error);
            file.close();
            std::filesystem::remove(temporary);
            return false;
        }
    }

    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary);
        return false;
    }
    return true;
}

NesKeyMap nes_load_key_map(std::error_code &error)
{
    NesKeyMap map = NesKeyMap::defaults();
    if (!map.load({}, error) && error == std::errc::no_such_file_or_directory) error.clear();
    return map;
}

bool nes_save_key_map(const NesKeyMap &map, std::error_code &error)
{
    return map.save({}, error);
}

std::string nes_key_label(std::uint32_t key)
{
    if (key == 0) return "Unassigned";
    if (key == ' ') return "Space";
    if (key == '\t') return "Tab";
    if (key == '\n') return "Enter";
    if (key < 0x80 && std::isprint(static_cast<unsigned char>(key))) {
        char character = static_cast<char>(key);
        if (character >= 'a' && character <= 'z') character = static_cast<char>(character - 'a' + 'A');
        return std::string(1, character);
    }
    std::ostringstream stream;
    stream << "Key 0x" << std::uppercase << std::hex << static_cast<unsigned long long>(key);
    return stream.str();
}
