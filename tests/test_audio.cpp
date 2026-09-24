#include "emulator_test_rom.hpp"

#include <SDL.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace {

void run_session(const std::filesystem::path &rom, const char *driver, bool available)
{
    assert(SDL_setenv("SDL_AUDIODRIVER", driver, 1) == 0);
    NesEmulator emulator;
    assert(emulator.start(rom.string()));
    emulator_test::await_frame(emulator);
    assert((SDL_WasInit(SDL_INIT_AUDIO) != 0) == available);
    if (available) assert(std::strcmp(SDL_GetCurrentAudioDriver(), driver) == 0);
    emulator.stop();
    assert(emulator_test::await_finished(emulator) == 0);
}

bool contains_generated_audio(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    std::int16_t sample = 0;
    int generated_samples = 0;
    while (file.read(reinterpret_cast<char *>(&sample), sizeof(sample))) {
        if (sample != 0 && ++generated_samples >= 128) return true;
    }
    return false;
}

void test_pcm_output(const std::filesystem::path &rom, const std::filesystem::path &recording)
{
    assert(SDL_setenv("SDL_AUDIODRIVER", "disk", 1) == 0);
    assert(SDL_setenv("SDL_DISKAUDIOFILE", recording.c_str(), 1) == 0);
    assert(SDL_setenv("SDL_AUDIO_FORMAT", "S16SYS", 1) == 0);
    NesEmulator emulator;
    assert(emulator.start(rom.string()));
    emulator_test::await_frame(emulator);
    assert(std::strcmp(SDL_GetCurrentAudioDriver(), "disk") == 0);
    for (int retry = 0; retry < 100 && !contains_generated_audio(recording); ++retry) SDL_Delay(10);
    emulator.stop();
    assert(emulator_test::await_finished(emulator) == 0);
    assert(contains_generated_audio(recording));
    assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
}

}  // namespace

int main()
{
    const auto directory = std::filesystem::temp_directory_path() /
                           ("lilygo-ui-nes-audio-" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::create_directories(directory);
    const auto rom = directory / "audio.nes";
    emulator_test::write_rom(rom);
    assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);

    run_session(rom, "dummy", true);
    assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
    run_session(rom, "dummy", true);
    assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);

    assert(SDL_InitSubSystem(SDL_INIT_AUDIO) == 0);
    run_session(rom, "dummy", true);
    assert(SDL_WasInit(SDL_INIT_AUDIO) == SDL_INIT_AUDIO);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);

    run_session(rom, "nes-test-unavailable", false);
    assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
    run_session(rom, "dummy", true);
    assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
    test_pcm_output(rom, directory / "output.raw");
    std::filesystem::remove_all(directory);
}
