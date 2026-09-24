#ifndef LILYGO_UI_NES_EMULATOR_NES_EMULATOR_HPP
#define LILYGO_UI_NES_EMULATOR_NES_EMULATOR_HPP

#include <cstddef>
#include <cstdint>
#include <string>

enum class NesButton : unsigned {
    up = 0,
    down,
    left,
    right,
    select,
    start,
    a,
    b,
};

class NesEmulator {
public:
    static constexpr int frame_width  = 256;
    static constexpr int frame_height = 240;
    static constexpr std::size_t frame_pixels =
        static_cast<std::size_t>(frame_width) * static_cast<std::size_t>(frame_height);

    NesEmulator() = default;
    ~NesEmulator();

    NesEmulator(const NesEmulator &)            = delete;
    NesEmulator &operator=(const NesEmulator &) = delete;

    bool start(const std::string &rom_path);
    void stop() noexcept;
    void set_button(NesButton button, bool pressed) noexcept;

    [[nodiscard]] bool running() const noexcept;
    bool consume_finished(int &result) noexcept;
    bool copy_latest_frame(std::uint16_t *destination, std::size_t pixels) noexcept;
};

#endif
