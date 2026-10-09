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

enum class NesStateOperation { save, load };

struct NesStateResult {
    NesStateOperation operation = NesStateOperation::save;
    bool success = false;
    std::string message;
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
    // Turbo bindings (C/D) pulse the corresponding A/B input on alternating
    // frames while held.  Only A and B are meaningful rapid buttons.
    void set_rapid_button(NesButton button, bool pressed) noexcept;

    // File I/O and core serialization run on the emulator worker at a frame
    // boundary. Consume the previous result before requesting another action.
    bool request_save_state(const std::string &path);
    bool request_load_state(const std::string &path);
    bool consume_state_result(NesStateResult &result);
    [[nodiscard]] bool state_operation_pending() const noexcept;

    [[nodiscard]] bool running() const noexcept;
    bool consume_finished(int &result) noexcept;
    bool copy_latest_frame(std::uint16_t *destination, std::size_t pixels) noexcept;
};

#endif
