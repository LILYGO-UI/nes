#ifndef LILYGO_UI_NES_TESTS_EMULATOR_TEST_ROM_HPP
#define LILYGO_UI_NES_TESTS_EMULATOR_TEST_ROM_HPP

#include "emulator/nes_emulator.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace emulator_test {

inline void write_rom(const std::filesystem::path &path)
{
    std::vector<std::uint8_t> bytes(16 + 16384 + 8192, 0);
    const std::array<std::uint8_t, 16> header{'N', 'E', 'S', 0x1a, 1, 1};
    std::copy(header.begin(), header.end(), bytes.begin());
    // A mapper-0 ROM that enables a sustained pulse and loops at $801b.
    const std::array<std::uint8_t, 30> program{
        0x78, 0xd8,
        0xa9, 0x01, 0x8d, 0x15, 0x40,
        0xa9, 0xbf, 0x8d, 0x00, 0x40,
        0xa9, 0xf8, 0x8d, 0x02, 0x40,
        0xa9, 0x01, 0x8d, 0x03, 0x40,
        0xa9, 0x00, 0x8d, 0x01, 0x20,
        0x4c, 0x1b, 0x80};
    std::copy(program.begin(), program.end(), bytes.begin() + 16);
    for (int vector : {0x3ffa, 0x3ffc, 0x3ffe}) bytes[16 + vector + 1] = 0x80;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    assert(output.good());
}

inline int await_finished(NesEmulator &emulator)
{
    int result = -999;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!emulator.consume_finished(result) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(result != -999);
    assert(!emulator.running());
    return result;
}

inline void await_frame(NesEmulator &emulator)
{
    std::array<std::uint16_t, NesEmulator::frame_pixels> frame{};
    int frames = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    // The first frame can be the reset buffer before the core starts.
    while (frames < 3 && std::chrono::steady_clock::now() < deadline) {
        if (emulator.copy_latest_frame(frame.data(), frame.size())) ++frames;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(frames >= 3);
    assert(emulator.running());
}

}  // namespace emulator_test

#endif
