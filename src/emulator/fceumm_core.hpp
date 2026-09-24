#ifndef LILYGO_UI_NES_EMULATOR_FCEUMM_CORE_HPP
#define LILYGO_UI_NES_EMULATOR_FCEUMM_CORE_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

struct FceummHost {
    int sample_rate = 48000;
    std::function<bool()> stop_requested;
    std::function<std::uint32_t()> buttons;
    std::function<int()> wait_frame;
    // Set the host's absolute frame clock to the loaded game's native rate.
    std::function<void(double)> set_frame_rate;
    std::function<void(const std::uint16_t *, std::size_t)> publish_frame;
    std::function<void(const std::int16_t *, std::size_t)> submit_audio;
};

// Runs on the emulator worker. Button bits use the NesButton enum order.
int run_fceumm(const std::string &rom_path, const FceummHost &host);

#endif
