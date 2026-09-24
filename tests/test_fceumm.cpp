#include "emulator/fceumm_core.hpp"
#include "emulator/nes_emulator.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

void write_bytes(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes)
{
    std::ofstream output(path, std::ios::binary);
    assert(output);
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    assert(output);
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<std::uint8_t> make_rom(bool battery = false, bool pal = false, bool nes2 = false)
{
    std::vector<std::uint8_t> rom(16 + 16384 + 8192, 0);
    const std::array<std::uint8_t, 16> header{'N', 'E', 'S', 0x1a, 1, 1,
                                             static_cast<std::uint8_t>(battery ? 0x22 : 0)};
    std::copy(header.begin(), header.end(), rom.begin());
    if (nes2) {
        rom[7] = 8;
        rom[10] = battery ? 0x70 : 0;
        rom[12] = pal ? 1 : 0;
    } else {
        rom[9] = pal ? 1 : 0;
    }
    // This self-contained ROM plays a pulse and copies the serial joypad bits
    // into SRAM, leaving a reset counter to verify battery restoration.
    std::vector<std::uint8_t> program{
        0x78, 0xd8, 0xa2, 0xff, 0x9a,
        0xa9, 0x00, 0x8d, 0x00, 0x20, 0x8d, 0x01, 0x20,
        0xee, 0x08, 0x60,
        0xa9, 0x3f, 0x8d, 0x06, 0x20,
        0xa9, 0x00, 0x8d, 0x06, 0x20,
        0xa9, 0x21, 0x8d, 0x07, 0x20,
        0xa9, 0x00, 0x8d, 0x06, 0x20, 0x8d, 0x06, 0x20,
        0xa9, 0x01, 0x8d, 0x15, 0x40,
        0xa9, 0xbf, 0x8d, 0x00, 0x40,
        0xa9, 0xf8, 0x8d, 0x02, 0x40,
        0xa9, 0x01, 0x8d, 0x03, 0x40,
    };
    const auto poll_address = static_cast<std::uint16_t>(0x8000 + program.size());
    const std::array<std::uint8_t, 12> strobe{
        0xa9, 0x01, 0x8d, 0x16, 0x40, 0xa9, 0x00, 0x8d, 0x16, 0x40, 0xa2, 0x00};
    program.insert(program.end(), strobe.begin(), strobe.end());
    const std::array<std::uint8_t, 13> poll{
        0xad, 0x16, 0x40, 0x29, 0x01, 0x9d, 0x00, 0x60, 0xe8, 0xe0, 0x08, 0xd0, 0xf3};
    program.insert(program.end(), poll.begin(), poll.end());
    program.push_back(0x4c);
    program.push_back(static_cast<std::uint8_t>(poll_address));
    program.push_back(static_cast<std::uint8_t>(poll_address >> 8));
    std::copy(program.begin(), program.end(), rom.begin() + 16);
    for (int vector = 0x3ffa; vector <= 0x3ffe; vector += 2) {
        rom[16 + vector] = 0x00;
        rom[16 + vector + 1] = 0x80;
    }
    return rom;
}

std::vector<std::uint8_t> make_animated_rom()
{
    auto rom = make_rom();
    const std::array<std::uint8_t, 23> reset{
        0x78, 0xd8, 0xa2, 0xff, 0x9a, 0xa9, 0x00,
        0x8d, 0x00, 0x20, 0x8d, 0x01, 0x20, 0x85, 0x00,
        0xa9, 0x80, 0x8d, 0x00, 0x20, 0x4c, 0x14, 0x80};
    const std::array<std::uint8_t, 37> nmi{
        0x48, 0x2c, 0x02, 0x20, 0xa9, 0x3f, 0x8d, 0x06, 0x20,
        0xa9, 0x00, 0x8d, 0x06, 0x20, 0xe6, 0x00, 0xa5, 0x00,
        0x29, 0x0f, 0x09, 0x20, 0x8d, 0x07, 0x20,
        0xa9, 0x20, 0x8d, 0x06, 0x20, 0xa9, 0x00, 0x8d, 0x06, 0x20,
        0x68, 0x40};
    std::copy(reset.begin(), reset.end(), rom.begin() + 16);
    std::copy(nmi.begin(), nmi.end(), rom.begin() + 16 + 0x100);
    rom[16 + 0x3ffa] = 0x00;
    rom[16 + 0x3ffb] = 0x81;
    return rom;
}

struct Session {
    int frames = 0;
    int audio_batches = 0;
    std::size_t samples = 0;
    bool nonzero_audio = false;
    double frame_rate = 0;
    std::uint16_t last_color = 0;
    std::vector<int> ticks;
    std::size_t tick = 0;
    std::uint32_t buttons = 0;

    FceummHost host(int rate = 48000)
    {
        FceummHost output;
        output.sample_rate = rate;
        output.stop_requested = [] { return false; };
        output.buttons = [this] { return buttons; };
        output.wait_frame = [this] { return tick < ticks.size() ? ticks[tick++] : 0; };
        output.set_frame_rate = [this](double value) {
            assert(std::isfinite(value) && value > 0);
            frame_rate = value;
        };
        output.publish_frame = [this](const std::uint16_t *pixels, std::size_t count) {
            assert(pixels && count == NesEmulator::frame_pixels);
            last_color = pixels[count / 2];
            ++frames;
        };
        output.submit_audio = [this](const std::int16_t *pcm, std::size_t count) {
            assert(pcm && count > 0);
            samples += count;
            ++audio_batches;
            nonzero_audio |= std::any_of(pcm, pcm + count, [](std::int16_t value) { return value != 0; });
        };
        return output;
    }
};

void test_timing_audio_and_rom_formats(const std::filesystem::path &directory)
{
    for (const bool nes2 : {false, true}) {
        for (const bool pal : {false, true}) {
            // FCEUmm detects legacy iNES PAL from its filename; NES 2.0 has
            // an explicit timing field and must work without a filename hint.
            const auto suffix = pal ? (nes2 ? "-pal.nes" : " (PAL).nes") : "-ntsc.nes";
            const auto path = directory / (std::string(nes2 ? "nes2" : "ines") + suffix);
            write_bytes(path, make_rom(false, pal, nes2));
            for (const int sample_rate : {32000, 48000}) {
                Session session;
                session.ticks.assign(90, 1);
                assert(run_fceumm(path.string(), session.host(sample_rate)) == 0);
                assert(session.frames == 90);
                assert(session.audio_batches > 0 && session.nonzero_audio);
                assert(session.frame_rate > (pal ? 49.0 : 59.0));
                assert(session.frame_rate < (pal ? 51.0 : 61.0));
                const double expected_samples = sample_rate * session.frames / session.frame_rate;
                // The core's first audio frame may be shorter than steady state.
                assert(std::abs(static_cast<double>(session.samples) - expected_samples) < sample_rate * 0.05);
                assert(session.last_color != 0);
                auto save = path;
                save.replace_extension(".sav");
                assert(!std::filesystem::exists(save));
            }
        }
    }
}

void test_long_audio_clock(const std::filesystem::path &directory)
{
    constexpr int warmup_frames = 60;
    constexpr int sample_rate = 48000;
    for (const bool pal : {false, true}) {
        const auto path = directory / (pal ? "audio-clock-pal.nes" : "audio-clock-ntsc.nes");
        write_bytes(path, make_rom(false, pal, true));
        const int measured_frames = pal ? 6000 : 7200;
        Session session;
        session.ticks.assign(warmup_frames + measured_frames, 1);
        auto host = session.host(sample_rate);
        const auto wait_frame = host.wait_frame;
        std::size_t warmup_samples = 0;
        host.wait_frame = [&] {
            // The next frame boundary follows both video and audio callbacks.
            if (session.tick == warmup_frames) warmup_samples = session.samples;
            return wait_frame();
        };
        assert(run_fceumm(path.string(), host) == 0);
        assert(session.frames == warmup_frames + measured_frames);
        assert(warmup_samples > 0 && session.samples > warmup_samples);
        const auto measured_samples = session.samples - warmup_samples;
        const double expected_samples = static_cast<double>(measured_frames) * sample_rate / session.frame_rate;
        const double error = static_cast<double>(measured_samples) - expected_samples;
        std::fprintf(stderr, "fceumm audio clock: %s, %d frames, %.9f Hz, %zu samples, error %+.3f samples\n",
                     pal ? "PAL" : "NTSC", measured_frames, session.frame_rate, measured_samples, error);
        // Allow one frame of flush jitter, but reject sustained clock drift.
        assert(std::abs(error) < sample_rate / session.frame_rate);
    }
}

void test_animation_and_catchup(const std::filesystem::path &directory)
{
    const auto path = directory / "animated.nes";
    write_bytes(path, make_animated_rom());
    Session session;
    session.ticks.assign(24, 1);
    auto host = session.host();
    std::set<std::uint16_t> colors;
    host.publish_frame = [&](const std::uint16_t *pixels, std::size_t count) {
        assert(pixels && count == NesEmulator::frame_pixels);
        if (++session.frames > 3) colors.insert(pixels[count / 2]);
    };
    assert(run_fceumm(path.string(), host) == 0);
    assert(session.frames == 24 && colors.size() >= 10);

    write_bytes(path, make_rom());
    Session catchup;
    catchup.ticks = {1, 1, 30, 1};
    assert(run_fceumm(path.string(), catchup.host()) == 0);
    assert(catchup.frames == 4);
    assert(catchup.samples > static_cast<std::size_t>(48000.0 / catchup.frame_rate * 4.8));
    assert(catchup.samples < static_cast<std::size_t>(48000.0 / catchup.frame_rate * 5.2));
}

void test_extended_mapper(const std::filesystem::path &directory)
{
    const auto base = make_rom(false, false, true);
    std::vector<std::uint8_t> rom(16 + 32768 + 8192, 0);
    std::copy_n(base.begin(), 16, rom.begin());
    std::copy_n(base.begin() + 16, 16384, rom.begin() + 16);
    std::copy_n(base.begin() + 16, 16384, rom.begin() + 16 + 16384);
    std::copy_n(base.begin() + 16 + 16384, 8192, rom.begin() + 16 + 32768);
    // Mapper 271 validates NES 2.0's upper mapper bits. Its initial 32 KiB
    // bank runs this test program.
    rom[4] = 2;
    rom[6] = 0xf0;
    rom[7] = 0x08;
    rom[8] = 0x01;
    const auto path = directory / "mapper271.nes";
    write_bytes(path, rom);
    Session session;
    session.ticks.assign(12, 1);
    assert(run_fceumm(path.string(), session.host()) == 0);
    assert(session.frames == 12 && session.last_color != 0);
    assert(session.nonzero_audio && session.samples > 8000);
}

void test_input_and_battery(const std::filesystem::path &directory)
{
    const auto path = directory / "input.nes";
    const auto save = directory / "input.sav";
    constexpr std::array<unsigned, 8> serial_bits{4, 5, 6, 7, 2, 3, 0, 1};
    for (const bool nes2 : {false, true}) {
        write_bytes(path, make_rom(true, false, nes2));
        for (unsigned button = 0; button < 8; ++button) {
            std::vector<std::uint8_t> before(8192, 0);
            before[8] = 42;
            before.back() = 0xa5;
            write_bytes(save, before);
            Session session;
            session.buttons = 1U << button;
            session.ticks = {1, 1, 1};
            assert(run_fceumm(path.string(), session.host()) == 0);
            const auto after = read_bytes(save);
            assert(after.size() == before.size());
            for (unsigned bit = 0; bit < 8; ++bit) assert(after[bit] == (bit == serial_bits[button] ? 1 : 0));
            assert(after[8] == 43 && after.back() == 0xa5);
        }
        Session restored;
        restored.ticks = {1, 1, 1};
        assert(run_fceumm(path.string(), restored.host()) == 0);
        const auto after = read_bytes(save);
        for (unsigned bit = 0; bit < 8; ++bit) assert(after[bit] == 0);
        assert(after[8] == 44 && after.back() == 0xa5);
    }
    std::filesystem::remove(save);
    Session fresh;
    fresh.ticks = {1, 1};
    assert(run_fceumm(path.string(), fresh.host()) == 0);
    assert(std::filesystem::file_size(save) == 8192);

    for (const std::size_t size : {std::size_t(0), std::size_t(4), std::size_t(8193)}) {
        std::vector<std::uint8_t> original(size, 0x5a);
        write_bytes(save, original);
        Session invalid;
        invalid.ticks = {1, 1};
        assert(run_fceumm(path.string(), invalid.host()) == 0);
        assert(read_bytes(save) == original);
    }
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        assert(entry.path().string().find(".tmp.") == std::string::npos);
    }
}

void test_invalid_roms_stop_and_restart(const std::filesystem::path &directory)
{
    const auto path = directory / "invalid.nes";
    const auto assert_invalid = [&](const std::vector<std::uint8_t> &bytes) {
        write_bytes(path, bytes);
        Session session;
        session.ticks = {1};
        assert(run_fceumm(path.string(), session.host()) != 0);
        assert(session.frames == 0 && session.samples == 0);
    };
    assert_invalid({});
    assert_invalid(std::vector<std::uint8_t>(32, 0x5a));
    auto rom = make_rom();
    rom.resize(20);
    assert_invalid(rom);
    rom = make_rom();
    rom[6] |= 0x04;
    rom.resize(16 + 511);
    assert_invalid(rom);
    rom = make_rom();
    rom[4] = 0;
    assert_invalid(rom);
    rom = make_rom(false, false, true);
    rom[4] = 0xff;
    rom[9] = 0x0f;
    assert_invalid(rom);
    rom = make_rom(false, false, true);
    rom[5] = 0xff;
    rom[9] = 0xf0;
    assert_invalid(rom);
    rom = make_rom(false, false, true);
    rom[6] = 0xf0;
    rom[7] = 0xf8;
    rom[8] = 0x0f;
    assert_invalid(rom);

    Session missing;
    missing.ticks = {1};
    assert(run_fceumm((directory / "missing.nes").string(), missing.host()) != 0);
    assert(missing.frames == 0);
    write_bytes(path, make_rom());
    for (const int sample_rate : {0, -1, 22050, 96001}) {
        Session invalid;
        invalid.ticks = {1};
        assert(run_fceumm(path.string(), invalid.host(sample_rate)) != 0);
        assert(invalid.frames == 0 && invalid.samples == 0);
    }
    Session stopped;
    stopped.ticks = {1};
    auto host = stopped.host();
    host.stop_requested = [] { return true; };
    assert(run_fceumm(path.string(), host) == 0);
    assert(stopped.frames == 0 && stopped.tick == 0);

    for (int repeat = 0; repeat < 3; ++repeat) {
        Session active;
        active.ticks.assign(10, 1);
        host = active.host();
        host.stop_requested = [&] { return active.frames >= 3; };
        assert(run_fceumm(path.string(), host) == 0);
        assert(active.frames == 3 && active.nonzero_audio);
    }
}

}  // namespace

int main()
{
    const auto directory = std::filesystem::temp_directory_path() /
                           ("lilygo-ui-nes-fceumm-" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::create_directories(directory);
    test_timing_audio_and_rom_formats(directory);
    test_long_audio_clock(directory);
    test_animation_and_catchup(directory);
    test_extended_mapper(directory);
    test_input_and_battery(directory);
    test_invalid_roms_stop_and_restart(directory);
    std::filesystem::remove_all(directory);
    return 0;
}
