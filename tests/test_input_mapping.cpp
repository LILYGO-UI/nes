#include "emulator/nes_input_mapping.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>

int main()
{
    const auto check = [](unsigned key, NesButton button, bool rapid = false) {
        const auto binding = nes_key_binding(key);
        assert(binding.valid);
        assert(binding.button == button);
        assert(binding.rapid == rapid);
    };

    check('w', NesButton::up);
    check('S', NesButton::down);
    check('a', NesButton::left);
    check('D', NesButton::right);
    check('k', NesButton::a);
    check('J', NesButton::b);
    check('i', NesButton::a, true);
    check('U', NesButton::b, true);
    check('1', NesButton::start);
    check('2', NesButton::select);

    assert(!nes_key_binding(' ').valid);
    assert(!nes_key_binding('4').valid);
    assert(!nes_key_binding('5').valid);  // P2 numeric bindings are unsupported.
    assert(!nes_key_binding(0x2191).valid);  // P2 arrow input is unsupported.

    NesKeyMap map;
    assert(map.key_for(NesMappingAction::up) == 'w');
    assert(map.binding('W').button == NesButton::up);
    assert(map.set_key(NesMappingAction::up, 'q') == NesKeyMapResult::applied);
    assert(map.set_key(NesMappingAction::down, 'q') == NesKeyMapResult::conflict);
    assert(map.set_key(NesMappingAction::down, 'q', NesKeyMapConflictPolicy::swap) == NesKeyMapResult::applied);
    assert(map.key_for(NesMappingAction::down) == 'q');
    assert(map.key_for(NesMappingAction::up) == 's');
    assert(map.action_for('Q') == NesMappingAction::down);
    assert(map.binding('q').button == NesButton::down);
    assert(map.binding('i').rapid == true);

    // Invalid actions and reserved key values leave the map unchanged.
    const auto before_left = map.key_for(NesMappingAction::left);
    assert(map.set_key(static_cast<NesMappingAction>(255), 'x') == NesKeyMapResult::invalid_action);
    assert(map.set_key(NesMappingAction::left, 0) == NesKeyMapResult::invalid_key);
    assert(map.set_key(NesMappingAction::left, std::numeric_limits<std::uint32_t>::max()) ==
           NesKeyMapResult::invalid_key);
    assert(map.key_for(NesMappingAction::left) == before_left);
    assert(map.clear_key(NesMappingAction::left));
    assert(map.key_for(NesMappingAction::left) == 0);
    assert(!map.binding('a').valid);
    assert(map.set_key(NesMappingAction::left, before_left) == NesKeyMapResult::applied);

    const auto path = std::filesystem::temp_directory_path() / "lilygo-ui-nes-input-map-test.conf";
    std::error_code error;
    std::filesystem::remove(path, error);
    assert(map.save(path, error));
    assert(!error);
    NesKeyMap loaded;
    assert(loaded.load(path, error));
    assert(loaded.key_for(NesMappingAction::up) == 's');
    assert(loaded.key_for(NesMappingAction::down) == 'q');
    std::filesystem::remove(path, error);

    // Explicit zero unassigns an action, while omitted fields keep defaults.
    {
        std::ofstream partial(path);
        partial << "# comments and unknown fields are allowed\n"
                << "up=120\n"
                << "left=0\n"
                << "future_option=42\n";
    }
    assert(loaded.load(path, error));
    assert(loaded.key_for(NesMappingAction::up) == 'x');
    assert(loaded.key_for(NesMappingAction::down) == 's');
    assert(loaded.key_for(NesMappingAction::left) == 0);
    assert(!loaded.binding('a').valid);
    std::filesystem::remove(path, error);

    // Duplicate assignments in a file are rejected without mutating the map.
    {
        std::ofstream malformed(path);
        malformed << "up=120\ndown=120\n";
    }
    assert(!loaded.load(path, error));
    assert(error == std::errc::invalid_argument);
    assert(loaded.key_for(NesMappingAction::up) == 'x');
    assert(loaded.key_for(NesMappingAction::left) == 0);
    std::filesystem::remove(path, error);
    return 0;
}
