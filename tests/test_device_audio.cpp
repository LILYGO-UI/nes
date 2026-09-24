#include "emulator_test_rom.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#include <signal.h>
#include <unistd.h>

namespace {

int run_test_player()
{
    const char *mode = std::getenv("NES_TEST_AUDIO_MODE");
    if (mode && std::strcmp(mode, "exit") == 0) return 0;
    if (mode && std::strcmp(mode, "stall") == 0) {
        signal(SIGTERM, SIG_IGN);
        for (;;) pause();
    }
    std::array<std::int16_t, 800> samples{};
    const char *recording = std::getenv("NES_TEST_AUDIO_RECORDING");
    std::ofstream output(recording ? recording : "/dev/null", std::ios::binary | std::ios::trunc);
    ssize_t bytes;
    while ((bytes = read(STDIN_FILENO, samples.data(), sizeof(samples))) > 0) {
        output.write(reinterpret_cast<const char *>(samples.data()), bytes);
        output.flush();
    }
    return 0;
}

bool contains_generated_audio(const std::filesystem::path &path)
{
    std::ifstream recording(path, std::ios::binary);
    std::int16_t sample = 0;
    while (recording.read(reinterpret_cast<char *>(&sample), sizeof(sample))) {
        if (sample != 0) return true;
    }
    return false;
}

void test_player_mode(const std::filesystem::path &rom, const std::filesystem::path &recording,
                      const char *mode)
{
    assert(setenv("NES_TEST_AUDIO_MODE", mode, 1) == 0);
    std::filesystem::remove(recording);
    NesEmulator emulator;
    assert(emulator.start(rom.string()));
    emulator_test::await_frame(emulator);
    // Fill every layer of buffering when the player never reads, then verify
    // that the emulation thread still publishes frames and stops promptly.
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    emulator_test::await_frame(emulator);
    if (std::strcmp(mode, "read") == 0) {
        for (int retry = 0; retry < 100 && !contains_generated_audio(recording); ++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(contains_generated_audio(recording));
    }
    const auto stop_start = std::chrono::steady_clock::now();
    emulator.stop();
    assert(emulator_test::await_finished(emulator) == 0);
    assert(std::chrono::steady_clock::now() - stop_start < std::chrono::seconds(1));
}

}  // namespace

int main(int argc, char *argv[])
{
    if (argc > 1 && std::strcmp(argv[1], "-q") == 0) return run_test_player();

    const auto directory = std::filesystem::temp_directory_path() /
                           ("nes-device-audio-" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::create_directories(directory);
    const auto rom = directory / "audio.nes";
    const auto recording = directory / "output.raw";
    emulator_test::write_rom(rom);
    assert(setenv("NES_AUDIO_PLAYER", "/nes-test-does-not-exist/aplay", 1) == 0);
    test_player_mode(rom, recording, "missing");

    const auto self = std::filesystem::absolute(argv[0]);
    assert(setenv("NES_AUDIO_PLAYER", self.c_str(), 1) == 0);
    assert(setenv("NES_TEST_AUDIO_RECORDING", recording.c_str(), 1) == 0);
    for (const auto *mode : {"read", "stall", "exit", "read"}) test_player_mode(rom, recording, mode);
    std::filesystem::remove_all(directory);
}
