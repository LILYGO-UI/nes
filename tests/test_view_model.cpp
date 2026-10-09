#include "pages/nes/nes_view_model.hpp"
#include "domain/nes_paths.hpp"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

void write_running_rom(const std::filesystem::path &path)
{
    std::vector<unsigned char> bytes(16 + 16384 + 8192, 0);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1a;
    bytes[4] = 1;
    bytes[5] = 1;
    bytes[16] = 0x4c;
    bytes[18] = 0x80;
    bytes[16 + 0x3ffb] = 0x80;
    bytes[16 + 0x3ffd] = 0x80;
    bytes[16 + 0x3fff] = 0x80;
    std::ofstream file(path, std::ios::binary);
    assert(file);
    file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

template <typename ViewModel>
void await_state_operation(ViewModel &model)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (model.state_operation_pending() && std::chrono::steady_clock::now() < deadline) {
        model.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(!model.state_operation_pending());
    assert(model.state() == NesUiState::running);
}

template <typename ViewModel>
void await_game_closed(ViewModel &model)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (model.state() == NesUiState::stopping && std::chrono::steady_clock::now() < deadline) {
        model.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(model.state() == NesUiState::library);
}

}  // namespace

int main()
{
    const auto directory = std::filesystem::temp_directory_path() /
                           ("lilygo-ui-nes-vm-" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "broken.nes", std::ios::binary) << "broken";
    const auto source_directory = directory / "source";
    std::filesystem::create_directory(source_directory);
    write_running_rom(source_directory / "added.NES");
    std::ofstream(source_directory / "invalid.nes", std::ios::binary) << "invalid";

    {
        // The page ViewModels work without the application facade or LVGL.
        const char *original_config_value = std::getenv("XDG_CONFIG_HOME");
        const bool had_config = original_config_value != nullptr;
        const std::string original_config = original_config_value ? original_config_value : "";
        const auto config_directory = directory / "page-config";
        assert(setenv("XDG_CONFIG_HOME", config_directory.c_str(), 1) == 0);
        assert(setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);

        NesUiStatus session;
        SettingsViewModel settings;
        GameViewModel game(session, settings);
        const auto destination = directory / "page-library";
        LibraryViewModel library(session, game, {destination.string()});
        RomBrowserViewModel browser(library);
        HelpViewModel help(library.rom_directory(), (directory / "page-states").string());
        assert(help.rom_directory() == destination.string());
        assert(help.states_directory() == (directory / "page-states").string());
        assert(library.roms().empty());
        assert(!game.active());
        assert(!game.save_state());
        assert(!game.load_state());
        assert(&game.keymap() == &settings.keymap());

        assert(!settings.mapping_capture());
        assert(settings.feedback() == SettingsFeedback::neutral);
        settings.begin_mapping_capture(NesMappingAction::a);
        assert(settings.mapping_capture() == NesMappingAction::a);
        assert(settings.feedback() == SettingsFeedback::action);
        assert(settings.status().find("Press a key for Jump A") == 0);
        settings.capture_mapping_key('w');
        assert(settings.mapping_capture() == NesMappingAction::a);
        assert(settings.feedback() == SettingsFeedback::error);
        assert(settings.status().find("Key is already assigned to Up") == 0);
        assert(settings.keymap().key(NesMappingAction::a) == 'k');
        settings.capture_mapping_key(0);
        assert(settings.mapping_capture() == NesMappingAction::a);
        settings.capture_mapping_key('q');
        assert(!settings.mapping_capture());
        assert(settings.feedback() == SettingsFeedback::success);
        assert(settings.status() == "Key mapping updated.");
        assert(game.keymap().key(NesMappingAction::a) == 'q');
        SettingsViewModel reloaded_settings;
        assert(reloaded_settings.keymap().key(NesMappingAction::a) == 'q');

        settings.begin_mapping_capture(NesMappingAction::b);
        settings.cancel_capture();
        settings.capture_mapping_key('e');
        assert(!settings.mapping_capture());
        assert(settings.feedback() == SettingsFeedback::neutral);
        assert(settings.status() == "Tap a control, then press a key on the keyboard.");
        assert(settings.keymap().key(NesMappingAction::b) == 'j');

        const auto invalid_config = directory / "config-file";
        std::ofstream(invalid_config) << "not a directory";
        assert(setenv("XDG_CONFIG_HOME", invalid_config.c_str(), 1) == 0);
        settings.begin_mapping_capture(NesMappingAction::b);
        settings.capture_mapping_key('e');
        assert(!settings.mapping_capture());
        assert(settings.feedback() == SettingsFeedback::warning);
        assert(settings.status().find("Key mapping updated, but could not save:") == 0);
        assert(game.keymap().key(NesMappingAction::b) == 'e');
        assert(setenv("XDG_CONFIG_HOME", config_directory.c_str(), 1) == 0);

        assert(browser.browse_directory(source_directory.string()));
        assert(browser.browser_entries().size() == 2);
        assert(!browser.import_rom((source_directory / "invalid.nes").string()));
        assert(library.state() == NesUiState::error);
        assert(library.status() == "Invalid iNES ROM");
        assert(library.error_revision() == 1);
        assert(browser.browser_path() == source_directory.string());
        assert(browser.import_rom((source_directory / "added.NES").string()));
        assert(library.roms().size() == 1);
        assert(library.state() == NesUiState::library);
        assert(library.start_rom(0));
        assert(game.active());
        assert(game.state() == NesUiState::running);
        assert(!browser.import_rom((source_directory / "added.NES").string()));
        assert(!library.delete_rom(0));
        game.stop_rom();
        library.refresh_library();
        assert(game.state() == NesUiState::stopping);
        await_game_closed(game);
        assert(!game.active());
        assert(library.delete_rom(0));
        assert(library.roms().empty());
        assert(!std::filesystem::exists(destination / "added.NES"));
        assert(std::filesystem::exists(source_directory / "added.NES"));

        if (had_config) assert(setenv("XDG_CONFIG_HOME", original_config.c_str(), 1) == 0);
        else assert(unsetenv("XDG_CONFIG_HOME") == 0);
    }
    {
        const char *original_home_value = std::getenv("HOME");
        const bool had_home = original_home_value != nullptr;
        const std::string original_home = original_home_value ? original_home_value : "";
        const auto home_directory = directory / "home";
        const auto rom_directory = home_directory / "nes" / "rom";
        std::filesystem::create_directory(home_directory);
        assert(setenv("HOME", home_directory.c_str(), 1) == 0);
        assert(!std::filesystem::exists(rom_directory));

        NesViewModel view_model;
        assert(view_model.rom_directory() == rom_directory.string());
        assert(std::filesystem::is_directory(rom_directory));
        assert(view_model.roms().empty());
        assert(view_model.state() == NesUiState::library);
        assert(view_model.status().empty());
        assert(view_model.error_revision() == 0);
        assert(view_model.states_directory() == (home_directory / "nes" / "states").string());
        assert(!view_model.save_state());
        assert(!view_model.load_state());

        assert(std::filesystem::remove(rom_directory));
        view_model.refresh_library();
        assert(std::filesystem::is_directory(rom_directory));
        assert(view_model.state() == NesUiState::library);
        assert(view_model.status().empty());
        assert(view_model.error_revision() == 0);

        assert(std::filesystem::remove(rom_directory));
        std::ofstream(rom_directory) << "existing file";
        view_model.refresh_library();
        assert(std::filesystem::is_regular_file(rom_directory));
        assert(view_model.roms().empty());
        assert(view_model.state() == NesUiState::error);
        assert(view_model.status().find("Could not create ROM folder:") == 0);
        assert(view_model.error_revision() == 1);
        const auto directory_error_message = view_model.status();
        view_model.refresh_library();
        assert(view_model.status() == directory_error_message);
        assert(view_model.error_revision() == 2);

        assert(std::filesystem::remove(rom_directory));
        view_model.refresh_library();
        assert(std::filesystem::is_directory(rom_directory));
        assert(view_model.state() == NesUiState::library);
        assert(view_model.status().empty());
        assert(view_model.error_revision() == 2);

        if (had_home) assert(setenv("HOME", original_home.c_str(), 1) == 0);
        else assert(unsetenv("HOME") == 0);
    }
    {
        const auto second_directory = directory / "second";
        NesViewModel view_model({directory.string(), second_directory.string()});
        assert(view_model.roms().size() == 1);
        assert(view_model.status().empty());
        assert(view_model.error_revision() == 0);
        assert(view_model.selected_rom());
        assert(!view_model.start_rom(0));
        assert(view_model.state() == NesUiState::error);
        assert(view_model.status() == "Invalid iNES ROM");
        assert(view_model.error_revision() == 1);
        assert(!view_model.start_rom(0));
        assert(view_model.status() == "Invalid iNES ROM");
        assert(view_model.error_revision() == 2);
        assert(!view_model.start_rom(4));
        assert(!view_model.poll());
        assert(view_model.error_revision() == 2);
        view_model.refresh_library();
        assert(view_model.state() == NesUiState::library);
        assert(view_model.status().empty());
        assert(view_model.error_revision() == 2);

        assert(!view_model.delete_rom(4));
        assert(view_model.state() == NesUiState::error);
        assert(view_model.status().find("Could not delete ROM:") == 0);
        assert(view_model.roms().size() == 1);
        assert(view_model.error_revision() == 3);
        const auto delete_error_message = view_model.status();
        assert(!view_model.delete_rom(4));
        assert(view_model.status() == delete_error_message);
        assert(view_model.error_revision() == 4);

        assert(view_model.delete_rom(0));
        assert(!std::filesystem::exists(directory / "broken.nes"));
        assert(view_model.roms().empty());
        assert(!view_model.selected_rom());
        assert(view_model.state() == NesUiState::library);
        assert(view_model.status().empty());
        assert(view_model.error_revision() == 4);
        view_model.refresh_library();
        assert(view_model.roms().empty());
        assert(view_model.status().empty());
        assert(view_model.error_revision() == 4);

        std::filesystem::create_directory(second_directory);
        std::ofstream(second_directory / "added.NES", std::ios::binary) << "broken";
        view_model.refresh_library();
        assert(view_model.roms().size() == 1);
        assert(view_model.delete_rom(0));
        assert(view_model.roms().empty());

        std::ofstream(directory / "unavailable.nes", std::ios::binary) << "broken";
        view_model.refresh_library();
        std::filesystem::remove(directory / "unavailable.nes");
        std::filesystem::create_directory(directory / "unavailable.nes");
        assert(!view_model.delete_rom(0));
        assert(view_model.state() == NesUiState::error);
        assert(view_model.status().find("Could not delete ROM:") == 0);
        assert(view_model.roms().size() == 1);
        assert(std::filesystem::is_directory(directory / "unavailable.nes"));
        assert(view_model.error_revision() == 5);
    }
    {
        const auto destination = directory / "imported";
        NesViewModel view_model({destination.string()});
        assert(std::filesystem::is_directory(destination));
        assert(view_model.roms().empty());
        assert(view_model.error_revision() == 0);
        assert(!view_model.import_rom((source_directory / "invalid.nes").string()));
        assert(view_model.state() == NesUiState::error);
        assert(view_model.status() == "Invalid iNES ROM");
        assert(view_model.error_revision() == 1);
        assert(!view_model.import_rom((source_directory / "invalid.nes").string()));
        assert(view_model.status() == "Invalid iNES ROM");
        assert(view_model.error_revision() == 2);
        assert(view_model.import_rom((source_directory / "added.NES").string()));
        assert(view_model.state() == NesUiState::library);
        assert(view_model.status() == "ROM added");
        assert(view_model.error_revision() == 2);
        assert(view_model.roms().size() == 1);
        assert(view_model.selected_rom());
        assert(view_model.selected_rom()->path == (destination / "added.NES").string());
        assert(std::filesystem::exists(source_directory / "added.NES"));
        assert(!view_model.import_rom((source_directory / "added.NES").string()));
        assert(view_model.state() == NesUiState::error);
        assert(view_model.status() == "A ROM with this filename already exists");
        assert(view_model.error_revision() == 3);
        assert(!view_model.import_rom((source_directory / "missing.nes").string()));
        assert(view_model.status().find("Could not add ROM:") == 0);
        assert(view_model.error_revision() == 4);

        assert(view_model.browse_directory(source_directory.string() + "/"));
        assert(view_model.browser_path() == source_directory.string());
        assert(view_model.browser_entries().size() == 2);
        assert(view_model.browser_entries()[0].name == "added.NES");
        assert(!view_model.browse_directory((directory / "missing").string()));
        assert(view_model.browser_path() == source_directory.string());
        assert(view_model.browser_entries().size() == 2);
        assert(view_model.browser_status().find("Could not open folder:") == 0);
        view_model.browse_parent();
        assert(view_model.browser_path() == directory.string());
        assert(view_model.browse_directory("/"));
        view_model.browse_parent();
        assert(view_model.browser_path() == "/");
        assert(view_model.browser_status().empty());
        assert(view_model.error_revision() == 4);

        const char *original_home_value = std::getenv("HOME");
        const bool had_home = original_home_value != nullptr;
        const std::string original_home = original_home_value ? original_home_value : "";
        assert(setenv("HOME", source_directory.c_str(), 1) == 0);
        view_model.open_browser();
        assert(view_model.browser_path() == source_directory.string());
        assert(view_model.browser_entries().size() == 2);
        assert(setenv("HOME", (directory / "missing").c_str(), 1) == 0);
        view_model.open_browser();
        assert(view_model.browser_path() == "/");
        assert(view_model.browser_status().empty());
        assert(unsetenv("HOME") == 0);
        view_model.open_browser();
        assert(view_model.browser_path() == "/");
        if (had_home) assert(setenv("HOME", original_home.c_str(), 1) == 0);
        else assert(unsetenv("HOME") == 0);

        for (std::size_t index = 1; index < RomLibrary::maximum_roms; ++index) {
            std::ofstream(destination / ("existing-" + std::to_string(index) + ".nes")) << "invalid";
        }
        view_model.refresh_library();
        write_running_rom(source_directory / "overflow.nes");
        assert(!view_model.import_rom((source_directory / "overflow.nes").string()));
        assert(view_model.state() == NesUiState::error);
        assert(view_model.status() == "ROM library is full (128 files)");
        assert(view_model.roms().size() == RomLibrary::maximum_roms);
        assert(!std::filesystem::exists(destination / "overflow.nes"));
        assert(view_model.error_revision() == 5);
    }
    {
        NesViewModel view_model(std::vector<std::string>{});
        assert(view_model.state() == NesUiState::error);
        assert(view_model.status() == "HOME directory is unavailable");
        assert(view_model.error_revision() == 1);
        assert(!view_model.import_rom((source_directory / "added.NES").string()));
        assert(view_model.state() == NesUiState::error);
        assert(view_model.status() == "Could not add ROM: HOME directory is unavailable");
        assert(view_model.roms().empty());
        assert(view_model.error_revision() == 2);
    }
    {
        const char *original_home_value = std::getenv("HOME");
        const bool had_home = original_home_value != nullptr;
        const std::string original_home = original_home_value ? original_home_value : "";
        const auto state_home = directory / "state-home";
        const auto rom_directory = state_home / "nes" / "rom";
        std::filesystem::create_directories(rom_directory);
        assert(setenv("HOME", state_home.c_str(), 1) == 0);
        write_running_rom(rom_directory / "running.nes");
        assert(setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
        const auto state_path = std::filesystem::path(nes_paths::state_path((rom_directory / "running.nes").string()));
        {
            NesViewModel view_model;
            assert(view_model.rom_directory() == rom_directory.string());
            assert(view_model.states_directory() == state_path.parent_path().string());
            assert(view_model.roms().size() == 1);
            assert(view_model.start_rom(0));
            assert(view_model.status().empty());
            assert(view_model.error_revision() == 0);
            assert(!view_model.start_rom(0));
            assert(view_model.load_state());
            assert(view_model.state_operation_pending());
            assert(!view_model.save_state());
            assert(!view_model.load_state());
            await_state_operation(view_model);
            assert(view_model.state_status_error());
            assert(!view_model.status().empty());
            assert(view_model.error_revision() == 0);

            assert(view_model.save_state());
            assert(view_model.status() == "Saving...");
            assert(!view_model.state_status_error());
            assert(!view_model.save_state());
            // A library refresh must never change the active game's save identity.
            view_model.refresh_library();
            await_state_operation(view_model);
            assert(!view_model.state_status_error());
            assert(std::filesystem::is_regular_file(state_path));
            assert(std::filesystem::file_size(state_path) > 0);

            assert(view_model.load_state());
            assert(view_model.status() == "Loading...");
            await_state_operation(view_model);
            assert(!view_model.state_status_error());

            const auto backup_path = state_path.string() + ".backup";
            std::filesystem::rename(state_path, backup_path);
            std::ofstream(state_path, std::ios::binary) << "invalid saved state";
            assert(view_model.load_state());
            await_state_operation(view_model);
            assert(view_model.state_status_error());
            assert(view_model.error_revision() == 0);
            std::filesystem::remove(state_path);
            std::filesystem::rename(backup_path, state_path);

            // A storage failure leaves the game and previously saved progress usable.
            const auto saved_states_directory = state_home / "states-backup";
            std::filesystem::rename(state_path.parent_path(), saved_states_directory);
            std::ofstream(state_path.parent_path()) << "not a directory";
            assert(view_model.save_state());
            await_state_operation(view_model);
            assert(view_model.state_status_error());
            assert(view_model.error_revision() == 0);
            assert(std::filesystem::is_regular_file(saved_states_directory / state_path.filename()));
            std::filesystem::remove(state_path.parent_path());
            std::filesystem::rename(saved_states_directory, state_path.parent_path());

            view_model.stop_rom();
            assert(!view_model.save_state());
            assert(!view_model.load_state());
            await_game_closed(view_model);
            assert(view_model.start_rom(0));
            assert(view_model.load_state());
            await_state_operation(view_model);
            assert(!view_model.state_status_error());
            assert(!view_model.delete_rom(0));
            assert(view_model.state() == NesUiState::running);
            assert(view_model.status() == "Close the game before deleting ROMs");
            assert(std::filesystem::exists(rom_directory / "running.nes"));
            assert(view_model.roms().size() == 1);
            assert(!view_model.import_rom((source_directory / "added.NES").string()));
            assert(view_model.state() == NesUiState::running);
            assert(view_model.status() == "Close the game before adding ROMs");
            assert(!std::filesystem::exists(rom_directory / "added.NES"));
            view_model.stop_rom();
            assert(!view_model.delete_rom(0));
            assert(view_model.state() == NesUiState::stopping);
            assert(std::filesystem::exists(rom_directory / "running.nes"));
            assert(!view_model.import_rom((source_directory / "added.NES").string()));
            assert(view_model.state() == NesUiState::stopping);
            assert(!std::filesystem::exists(rom_directory / "added.NES"));
            assert(view_model.error_revision() == 0);
            await_game_closed(view_model);
        }
        // Reconstruct the frontend to prove the state survives an app session.
        {
            NesViewModel reopened;
            assert(reopened.start_rom(0));
            assert(reopened.status().empty());
            assert(!reopened.state_operation_pending());
            assert(!reopened.state_status_error());
            assert(reopened.load_state());
            await_state_operation(reopened);
            assert(!reopened.state_status_error());
            assert(reopened.status() == "State loaded");
            reopened.stop_rom();
            await_game_closed(reopened);
        }
        if (had_home) assert(setenv("HOME", original_home.c_str(), 1) == 0);
        else assert(unsetenv("HOME") == 0);
    }
    std::filesystem::remove_all(directory);
    return 0;
}
