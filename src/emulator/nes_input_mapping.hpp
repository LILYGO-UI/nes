#ifndef LILYGO_UI_NES_EMULATOR_NES_INPUT_MAPPING_HPP
#define LILYGO_UI_NES_EMULATOR_NES_INPUT_MAPPING_HPP

#include "emulator/nes_emulator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

/**
 * The ten configurable P1 actions. The rapid actions correspond to the C/D
 * aliases from the product mapping: rapid_a pulses the NES A button and
 * rapid_b pulses the NES B button. P2 is intentionally not represented.
 */
enum class NesMappingAction : std::uint8_t {
    up = 0,
    down,
    left,
    right,
    a,
    b,
    rapid_a,
    rapid_b,
    start,
    select,
    // Descriptive aliases for callers that use the labels from the product
    // table. They intentionally share the canonical action values above.
    jump_a   = a,
    attack_b = b,
    rapid_c  = rapid_a,
    rapid_d  = rapid_b,
    count = 10,
};

constexpr std::size_t nes_mapping_action_count = static_cast<std::size_t>(NesMappingAction::count);

/** A P1 keyboard binding returned by a key map lookup. */
struct NesKeyBinding {
    NesButton button = NesButton::up;
    bool rapid        = false;
    bool valid        = false;
};

/** How set_key handles a key that is already assigned to another action. */
enum class NesKeyMapConflictPolicy : std::uint8_t {
    reject,
    swap,
};

enum class NesKeyMapResult : std::uint8_t {
    applied,
    conflict,
    invalid_action,
    invalid_key,
};

/**
 * Return a canonical representation of a keyboard key.
 *
 * Keyboard drivers used by the simulator and the device disagree about the
 * case of ASCII letters. Storing letters in lower-case makes a custom `q`
 * binding work for both `q` and `Q`; all other key codes are unchanged.
 */
constexpr std::uint32_t nes_normalize_key(std::uint32_t key) noexcept
{
    return key >= static_cast<std::uint32_t>('A') && key <= static_cast<std::uint32_t>('Z')
               ? key + (static_cast<std::uint32_t>('a') - static_cast<std::uint32_t>('A'))
               : key;
}

/** Zero is reserved for an unbound action. Other LVGL/Unicode key codes are accepted. */
constexpr bool nes_valid_key(std::uint32_t key) noexcept
{
    return key != 0 && key != UINT32_MAX;
}

constexpr bool nes_valid_mapping_action(NesMappingAction action) noexcept
{
    return static_cast<std::size_t>(action) < nes_mapping_action_count;
}

constexpr NesButton nes_mapping_button(NesMappingAction action) noexcept
{
    switch (action) {
    case NesMappingAction::up:
        return NesButton::up;
    case NesMappingAction::down:
        return NesButton::down;
    case NesMappingAction::left:
        return NesButton::left;
    case NesMappingAction::right:
        return NesButton::right;
    case NesMappingAction::a:
    case NesMappingAction::rapid_a:
        return NesButton::a;
    case NesMappingAction::b:
    case NesMappingAction::rapid_b:
        return NesButton::b;
    case NesMappingAction::start:
        return NesButton::start;
    case NesMappingAction::select:
        return NesButton::select;
    case NesMappingAction::count:
        break;
    }
    return NesButton::up;
}

constexpr bool nes_mapping_is_rapid(NesMappingAction action) noexcept
{
    return action == NesMappingAction::rapid_a || action == NesMappingAction::rapid_b;
}

/** Stable field names used by the on-disk keymap format and UI code. */
constexpr const char *nes_mapping_action_name(NesMappingAction action) noexcept
{
    switch (action) {
    case NesMappingAction::up:
        return "up";
    case NesMappingAction::down:
        return "down";
    case NesMappingAction::left:
        return "left";
    case NesMappingAction::right:
        return "right";
    case NesMappingAction::a:
        return "a";
    case NesMappingAction::b:
        return "b";
    case NesMappingAction::rapid_a:
        return "rapid_a";
    case NesMappingAction::rapid_b:
        return "rapid_b";
    case NesMappingAction::start:
        return "start";
    case NesMappingAction::select:
        return "select";
    case NesMappingAction::count:
        break;
    }
    return "";
}

/** Human-readable labels used by the settings page. */
constexpr const char *nes_mapping_action_label(NesMappingAction action) noexcept
{
    switch (action) {
    case NesMappingAction::up:
        return "Up";
    case NesMappingAction::down:
        return "Down";
    case NesMappingAction::left:
        return "Left";
    case NesMappingAction::right:
        return "Right";
    case NesMappingAction::a:
        return "Jump A";
    case NesMappingAction::b:
        return "Attack B";
    case NesMappingAction::rapid_a:
        return "Rapid C";
    case NesMappingAction::rapid_b:
        return "Rapid D";
    case NesMappingAction::start:
        return "Start";
    case NesMappingAction::select:
        return "Select";
    case NesMappingAction::count:
        break;
    }
    return "";
}

/**
 * Mutable P1 key map. Every action has at most one key and every key can be
 * assigned to at most one action. The defaults match the product mapping:
 * W/S/A/D, K/J, I/U and 1/2.
 */
class NesKeyMap {
public:
    NesKeyMap() noexcept;

    [[nodiscard]] static NesKeyMap defaults() noexcept;
    [[nodiscard]] static std::filesystem::path config_path();

    [[nodiscard]] std::uint32_t key(NesMappingAction action) const noexcept;
    [[nodiscard]] std::uint32_t key_for(NesMappingAction action) const noexcept { return key(action); }
    [[nodiscard]] const std::array<std::uint32_t, nes_mapping_action_count> &keys() const noexcept;

    /**
     * Assign a key. With reject (the default), an already assigned key leaves
     * the map unchanged and returns conflict. With swap, the two actions
     * exchange keys, which lets a UI implement a one-step remap safely.
     */
    NesKeyMapResult set_key(NesMappingAction action, std::uint32_t key,
                            NesKeyMapConflictPolicy policy = NesKeyMapConflictPolicy::reject) noexcept;
    bool clear_key(NesMappingAction action) noexcept;

    [[nodiscard]] bool has_key(std::uint32_t key, NesMappingAction ignored = NesMappingAction::count) const noexcept;
    [[nodiscard]] std::optional<NesMappingAction> action_for(std::uint32_t key) const noexcept;
    [[nodiscard]] NesKeyBinding binding(std::uint32_t key) const noexcept;

    /** Read/write the line-oriented keymap file. The object is unchanged on read failure. */
    bool load(const std::filesystem::path &path, std::error_code &error);
    bool save(const std::filesystem::path &path, std::error_code &error) const;

private:
    std::array<std::uint32_t, nes_mapping_action_count> keys_{};
};

/** Read the default path, returning defaults when no file exists. */
NesKeyMap nes_load_key_map(std::error_code &error);

/** Write to the default path, creating its parent directory when necessary. */
bool nes_save_key_map(const NesKeyMap &map, std::error_code &error);

/** Format a key code for the mapping settings page (e.g. `W`, `Space`). */
std::string nes_key_label(std::uint32_t key);

/**
 * Return the P1 binding for the built-in mapping for an LVGL/ASCII key code.
 * This constexpr overload remains for callers that only need defaults;
 * configurable input should call NesKeyMap::binding instead.
 *
 * The evdev driver emits lower-case ASCII unless Shift/Caps Lock is active,
 * while SDL text input can emit either case. Accept both so a held Shift does
 * not make the controls unexpectedly stop working.
 */
constexpr NesKeyBinding nes_key_binding(std::uint32_t key) noexcept
{
    switch (nes_normalize_key(key)) {
    case 'w':
        return {NesButton::up, false, true};
    case 's':
        return {NesButton::down, false, true};
    case 'a':
        return {NesButton::left, false, true};
    case 'd':
        return {NesButton::right, false, true};
    case 'k':
        return {NesButton::a, false, true};
    case 'j':
        return {NesButton::b, false, true};
    case 'i':
        return {NesButton::a, true, true};
    case 'u':
        return {NesButton::b, true, true};
    case '1':
        return {NesButton::start, false, true};
    case '2':
        return {NesButton::select, false, true};
    default:
        return {};
    }
}

#endif
