#include "emulator/nes_emulator.hpp"
#include "emulator/nes_performance.hpp"
#include "emulator_test_rom.hpp"
#include "domain/nes_paths.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

using Clock = std::chrono::steady_clock;

NesStateResult await_state_result(NesEmulator &emulator)
{
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (emulator.state_operation_pending() && Clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(!emulator.state_operation_pending());
    NesStateResult result;
    assert(emulator.consume_state_result(result));
    assert(!emulator.consume_state_result(result));
    assert(!result.message.empty());
    return result;
}

void test_state_requests(const std::filesystem::path &directory, const std::filesystem::path &rom)
{
    NesEmulator emulator;
    const auto state = nes_paths::state_path(rom.string());
    assert(!emulator.request_save_state(state));
    assert(!emulator.request_load_state(state));
    assert(!emulator.state_operation_pending());
    assert(emulator.start(rom.string()));
    emulator_test::await_frame(emulator);
    assert(!emulator.request_save_state(""));
    assert(!emulator.request_load_state(""));
    assert(emulator.request_save_state(state));
    assert(!emulator.request_save_state(state));
    assert(!emulator.request_load_state(state));
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (emulator.state_operation_pending() && Clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // A completed but unconsumed result cannot be replaced by a new request.
    assert(!emulator.state_operation_pending());
    assert(!emulator.request_load_state(state));
    auto result = await_state_result(emulator);
    assert(result.operation == NesStateOperation::save && result.success);
    assert(std::filesystem::file_size(state) > 1024);
    assert(emulator.request_load_state(state));
    result = await_state_result(emulator);
    assert(result.operation == NesStateOperation::load && result.success);
    emulator_test::await_frame(emulator);

    assert(emulator.request_load_state((directory / "missing.state").string()));
    result = await_state_result(emulator);
    assert(result.operation == NesStateOperation::load && !result.success);
    emulator_test::await_frame(emulator);
    const auto blocked = directory / "blocked";
    {
        std::ofstream output(blocked);
        output << "Preserve this file";
    }
    assert(emulator.request_save_state((blocked / "save.state").string()));
    result = await_state_result(emulator);
    assert(result.operation == NesStateOperation::save && !result.success);
    assert(std::filesystem::file_size(blocked) == 18);
    assert(emulator.request_load_state(state));
    assert(await_state_result(emulator).success);
    emulator.stop();
    assert(emulator_test::await_finished(emulator) == 0);

    for (int repeat = 0; repeat < 4; ++repeat) {
        assert(emulator.start(rom.string()));
        NesStateResult stale;
        assert(!emulator.consume_state_result(stale));
        assert(!emulator.state_operation_pending());
        assert(emulator.request_load_state(state));
        emulator.stop();
        assert(!emulator.request_save_state(state));
        assert(emulator_test::await_finished(emulator) == 0);
        assert(!emulator.state_operation_pending());
        // Stopping races with the worker's frame boundary. Either completion
        // or cancellation must produce one terminal result and release busy.
        const auto stopped = await_state_result(emulator);
        assert(stopped.operation == NesStateOperation::load);
    }

    assert(emulator.start(rom.string()));
    assert(emulator.request_save_state(state));
    emulator.stop();
    assert(emulator_test::await_finished(emulator) == 0);
    // Restart deliberately leaves the previous terminal result unconsumed.
    assert(emulator.start(rom.string()));
    NesStateResult stale;
    assert(!emulator.consume_state_result(stale));
    assert(!emulator.state_operation_pending());
    assert(emulator.request_load_state(state));
    assert(await_state_result(emulator).success);
    emulator_test::await_frame(emulator);
    emulator.stop();
    assert(emulator_test::await_finished(emulator) == 0);
}

}  // namespace

int main()
{
    const auto directory = std::filesystem::temp_directory_path() /
                           ("nes-emulator-" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::create_directories(directory);
    const auto isolated_home = directory / "home";
    std::filesystem::create_directories(isolated_home);
    assert(setenv("HOME", isolated_home.c_str(), 1) == 0);
    const auto rom = directory / "running.nes";
    emulator_test::write_rom(rom);
    assert(setenv("NES_AUDIO", "0", 1) == 0);
    assert(setenv("NES_PERF", "1", 1) == 0);
    test_state_requests(directory, rom);
    {
        NesEmulator emulator;
        std::array<std::uint16_t, NesEmulator::frame_pixels> frame{};
        for (int session = 0; session < 7; ++session) {
            assert(emulator.start(rom.string()));
            assert(!emulator.start(rom.string()));
            int frames          = 0;
            std::uint64_t emulated = 0;
            std::uint64_t published = 0;
            const auto collect = [&] {
                const auto stats = nes_performance::take_snapshot();
                emulated += stats.stages[static_cast<std::size_t>(nes_performance::Stage::emulation)].count;
                published += stats.stages[static_cast<std::size_t>(nes_performance::Stage::video_blit)].count;
            };
            const auto started = Clock::now();
            const auto deadline = started + std::chrono::seconds(3);
            // Reset/clear frames may arrive before the core has emulated anything.
            while ((frames < 5 || emulated < 3 || published < 3) && Clock::now() < deadline) {
                if (emulator.copy_latest_frame(frame.data(), frame.size())) ++frames;
                collect();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            assert(frames >= 5);
            emulator.set_button(NesButton::a, true);
            emulator.set_button(NesButton::up, true);
            emulator.set_button(NesButton::a, false);
            emulator.set_button(NesButton::up, false);
            const auto stop_started = Clock::now();
            emulator.stop();
            assert(emulator_test::await_finished(emulator) == 0);
            assert(Clock::now() - stop_started < std::chrono::seconds(1));
            collect();
            assert(emulated >= 3);
            assert(published >= 3);
            const double seconds = std::chrono::duration<double>(Clock::now() - started).count();
            assert(emulated <= seconds * 65 + 2);
        }
        for (int session = 0; session < 2; ++session) {
            assert(emulator.start((directory / "missing.nes").string()));
            assert(emulator_test::await_finished(emulator) != 0);
            assert(emulator.start(rom.string()));
            emulator.stop();
            assert(emulator_test::await_finished(emulator) == 0);
        }
    }
    std::filesystem::remove_all(directory);
}
