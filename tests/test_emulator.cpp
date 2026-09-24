#include "emulator/nes_emulator.hpp"
#include "emulator/nes_performance.hpp"
#include "emulator_test_rom.hpp"

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

}  // namespace

int main()
{
    const auto directory = std::filesystem::temp_directory_path() /
                           ("nes-emulator-" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::create_directories(directory);
    const auto rom = directory / "running.nes";
    emulator_test::write_rom(rom);
    assert(setenv("NES_AUDIO", "0", 1) == 0);
    assert(setenv("NES_PERF", "1", 1) == 0);
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
