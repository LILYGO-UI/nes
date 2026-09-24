#include "emulator/fceumm_core.hpp"

#include "emulator/nes_emulator.hpp"
#include "emulator/nes_performance.hpp"

#include <libretro.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <unistd.h>

namespace {

constexpr std::uintmax_t kMaxRomBytes = 64U * 1024U * 1024U;
constexpr unsigned kGetTargetSampleRate = 81 | RETRO_ENVIRONMENT_EXPERIMENTAL;
std::mutex core_mutex;

struct Session;
Session *session = nullptr;

struct Session {
    const FceummHost &host;
    std::unordered_map<std::string, std::string> options;
    std::string directory;
    std::array<std::uint16_t, NesEmulator::frame_pixels> frame{};
    std::array<std::int16_t, 4096> audio{};
    std::uint32_t buttons = 0;
    bool initialized = false;
    bool loaded = false;
    bool failed = false;
    bool shutdown = false;
    bool publish = true;
    bool rgb565 = false;

    explicit Session(const FceummHost &host_) : host(host_) {}
    ~Session()
    {
        if (loaded) retro_unload_game();
        if (initialized) retro_deinit();
        session = nullptr;
    }

    bool configure_timing(const retro_system_av_info &info)
    {
        if (!std::isfinite(info.timing.fps) || info.timing.fps < 40 || info.timing.fps > 70 ||
            info.timing.sample_rate != host.sample_rate ||
            info.geometry.base_width != NesEmulator::frame_width ||
            info.geometry.base_height != NesEmulator::frame_height) {
            std::fprintf(stderr, "fceumm: unsupported AV configuration: %ux%u, %.6f Hz, %.0f samples/s\n",
                         info.geometry.base_width, info.geometry.base_height,
                         info.timing.fps, info.timing.sample_rate);
            failed = true;
            return false;
        }
        host.set_frame_rate(info.timing.fps);
        return true;
    }
};

void core_log(retro_log_level level, const char *format, ...)
{
    if (level < RETRO_LOG_INFO) return;
    std::fprintf(stderr, "fceumm: ");
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
}

void set_options(const retro_core_option_definition *definitions)
{
    for (const auto *option = definitions; option && option->key; ++option) {
        const auto *value = option->default_value ? option->default_value : option->values[0].value;
        if (value) session->options[option->key] = value;
    }
    // Keep the complete native frame and let the ROM select its console timing.
    session->options["fceumm_region"] = "Auto";
    session->options["fceumm_overscan_h_left"] = "0";
    session->options["fceumm_overscan_h_right"] = "0";
    session->options["fceumm_overscan_v_top"] = "0";
    session->options["fceumm_overscan_v_bottom"] = "0";
    session->options["fceumm_sndrate_hint"] = "Auto";
}

bool environment(unsigned command, void *data)
{
    if (!session) return false;
    switch (command) {
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            *static_cast<unsigned *>(data) = 1;
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
            set_options(static_cast<const retro_core_option_definition *>(data));
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
            set_options(static_cast<const retro_core_options_intl *>(data)->us);
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            auto *variable = static_cast<retro_variable *>(data);
            const auto found = session->options.find(variable->key);
            variable->value = found == session->options.end() ? nullptr : found->second.c_str();
            return variable->value != nullptr;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *static_cast<bool *>(data) = false;
            return true;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *static_cast<bool *>(data) = true;
            return true;
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            session->rgb565 = *static_cast<retro_pixel_format *>(data) == RETRO_PIXEL_FORMAT_RGB565;
            return session->rgb565;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            static_cast<retro_log_callback *>(data)->log = core_log;
            return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *static_cast<const char **>(data) = session->directory.c_str();
            return true;
        case kGetTargetSampleRate:
            *static_cast<unsigned *>(data) = static_cast<unsigned>(session->host.sample_rate);
            return true;
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
            return session->configure_timing(*static_cast<const retro_system_av_info *>(data));
        case RETRO_ENVIRONMENT_SET_GEOMETRY: {
            const auto *geometry = static_cast<const retro_game_geometry *>(data);
            return geometry->base_width == NesEmulator::frame_width &&
                   geometry->base_height == NesEmulator::frame_height;
        }
        case RETRO_ENVIRONMENT_SHUTDOWN:
            session->shutdown = true;
            return true;
        case RETRO_ENVIRONMENT_SET_MESSAGE:
            std::fprintf(stderr, "fceumm: %s\n", static_cast<const retro_message *>(data)->msg);
            return true;
        default:
            return false;
    }
}

void video_refresh(const void *data, unsigned width, unsigned height, std::size_t pitch)
{
    if (!session || !data || !session->publish) return;
    if (!session->rgb565 || width != NesEmulator::frame_width || height != NesEmulator::frame_height ||
        pitch < width * sizeof(std::uint16_t)) {
        session->failed = true;
        return;
    }
    nes_performance::Scope timing(nes_performance::Stage::video_blit);
    const auto *source = static_cast<const std::uint8_t *>(data);
    for (unsigned y = 0; y < height; ++y) {
        std::memcpy(session->frame.data() + y * width, source + y * pitch, width * sizeof(std::uint16_t));
    }
    session->host.publish_frame(session->frame.data(), session->frame.size());
}

std::size_t audio_batch(const std::int16_t *data, std::size_t frames)
{
    if (!session || !frames) return frames;
    if (!data) {
        session->failed = true;
        return 0;
    }
    for (std::size_t offset = 0; offset < frames;) {
        const auto count = std::min(frames - offset, session->audio.size());
        {
            nes_performance::Scope timing(nes_performance::Stage::audio_generate);
            for (std::size_t index = 0; index < count; ++index) {
                const auto sample = (offset + index) * 2;
                session->audio[index] = static_cast<std::int16_t>(
                    (static_cast<std::int32_t>(data[sample]) + data[sample + 1]) / 2);
            }
        }
        if (session->host.submit_audio) session->host.submit_audio(session->audio.data(), count);
        offset += count;
    }
    return frames;
}

void audio_sample(std::int16_t left, std::int16_t right)
{
    const std::array<std::int16_t, 2> pair{left, right};
    audio_batch(pair.data(), 1);
}

void input_poll()
{
    if (session) session->buttons = session->host.buttons();
}

std::int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (!session || port != 0 || device != RETRO_DEVICE_JOYPAD || index != 0) return 0;
    constexpr std::array<unsigned, 8> ids{
        RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN,
        RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
        RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_DEVICE_ID_JOYPAD_START,
        RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B};
    for (std::size_t bit = 0; bit < ids.size(); ++bit) {
        if (id == ids[bit]) return (session->buttons & (1U << bit)) != 0;
    }
    return 0;
}

bool load_battery(const std::filesystem::path &path, void *memory, std::size_t size)
{
    std::error_code error;
    const auto file_size = std::filesystem::file_size(path, error);
    if (error == std::errc::no_such_file_or_directory) return true;
    if (!error && file_size == size) {
        std::vector<char> bytes(size);
        std::ifstream file(path, std::ios::binary);
        if (file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
            std::memcpy(memory, bytes.data(), size);
            return true;
        }
    }
    std::fprintf(stderr, "fceumm: cannot load battery RAM %s (expected %zu bytes); existing save will be preserved\n",
                 path.c_str(), size);
    return false;
}

bool save_battery(const std::filesystem::path &path, const void *memory, std::size_t size)
{
    std::string temporary = path.string() + ".tmp.XXXXXX";
    const int descriptor = mkstemp(temporary.data());
    if (descriptor < 0) {
        std::fprintf(stderr, "fceumm: cannot create battery RAM file %s: %s\n", path.c_str(), std::strerror(errno));
        return false;
    }
    bool written = false;
    if (std::FILE *file = fdopen(descriptor, "wb")) {
        written = std::fwrite(memory, 1, size, file) == size;
        if (std::fflush(file) != 0) written = false;
        if (written && fsync(descriptor) != 0) written = false;
        if (std::fclose(file) != 0) written = false;
    } else {
        close(descriptor);
    }
    if (written && std::rename(temporary.c_str(), path.c_str()) == 0) return true;
    std::fprintf(stderr, "fceumm: cannot save battery RAM %s: %s\n", path.c_str(), std::strerror(errno));
    std::remove(temporary.c_str());
    return false;
}

bool valid_rom_size(const std::array<std::uint8_t, 16> &header, std::uintmax_t file_size)
{
    const bool nes2 = (header[7] & 0x0cU) == 0x08U;
    const auto size_bytes = [nes2](unsigned count, unsigned upper, std::uintmax_t bank_size) {
        if (!nes2) return count * bank_size;
        if (upper != 0x0f) return (count | (upper << 8U)) * bank_size;
        // NES 2.0 also encodes byte counts as 2^exponent * (2*multiplier+1).
        const unsigned exponent = count >> 2U;
        if (exponent > 26) return kMaxRomBytes + 1;
        return (std::uintmax_t{1} << exponent) * ((count & 3U) * 2U + 1U);
    };
    const auto prg = size_bytes(header[4], header[9] & 0x0fU, 16384);
    const auto chr = size_bytes(header[5], header[9] >> 4U, 8192);
    if (!prg || prg > kMaxRomBytes || chr > kMaxRomBytes) return false;
    const auto required = header.size() + ((header[6] & 0x04U) ? 512U : 0U) + prg + chr;
    return required <= file_size && required <= kMaxRomBytes;
}

int run_session(const std::string &rom_path, const FceummHost &host)
{
    if (!host.stop_requested || !host.buttons || !host.wait_frame || !host.set_frame_rate || !host.publish_frame ||
        (host.sample_rate != 32000 && host.sample_rate != 44100 && host.sample_rate != 48000 && host.sample_rate != 96000)) {
        std::fprintf(stderr, "fceumm: invalid host configuration\n");
        return -1;
    }
    std::error_code error;
    const auto rom_size = std::filesystem::file_size(rom_path, error);
    std::array<std::uint8_t, 16> header{};
    std::ifstream rom(rom_path, std::ios::binary);
    if (error || rom_size < header.size() || rom_size > kMaxRomBytes ||
        !rom.read(reinterpret_cast<char *>(header.data()), header.size()) ||
        std::memcmp(header.data(), "NES\x1a", 4) != 0 || !valid_rom_size(header, rom_size)) {
        std::fprintf(stderr, "fceumm: cannot read a valid iNES ROM: %s\n", rom_path.c_str());
        return -1;
    }

    Session state(host);
    state.directory = std::filesystem::absolute(rom_path).parent_path().string();
    session = &state;
    retro_set_environment(environment);
    retro_set_video_refresh(video_refresh);
    retro_set_audio_sample(audio_sample);
    retro_set_audio_sample_batch(audio_batch);
    retro_set_input_poll(input_poll);
    retro_set_input_state(input_state);
    retro_init();
    state.initialized = true;
    const retro_game_info game{rom_path.c_str(), nullptr, 0, nullptr};
    if (!retro_load_game(&game)) {
        std::fprintf(stderr, "fceumm: could not load ROM: %s\n", rom_path.c_str());
        return -1;
    }
    state.loaded = true;
    retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    retro_system_av_info av{};
    retro_get_system_av_info(&av);
    if (!state.rgb565 || !state.configure_timing(av)) return -1;
    std::fprintf(stderr, "fceumm: %ux%u RGB565, %.6f Hz, %d Hz audio\n",
                 av.geometry.base_width, av.geometry.base_height, av.timing.fps, host.sample_rate);

    auto save_path = std::filesystem::path(rom_path);
    save_path.replace_extension(".sav");
    void *battery = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    const auto battery_size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    const bool save_enabled = battery && battery_size && load_battery(save_path, battery, battery_size);
    while (!host.stop_requested() && !state.shutdown && !state.failed) {
        const int elapsed = host.wait_frame();
        if (elapsed <= 0 || host.stop_requested()) break;
        const int frames = std::min(elapsed, 2);
        for (int index = 0; index < frames && !host.stop_requested() && !state.shutdown && !state.failed; ++index) {
            state.publish = index == frames - 1;
            nes_performance::Scope timing(nes_performance::Stage::emulation);
            retro_run();
        }
    }
    if (state.failed) return -1;
    if (save_enabled && !save_battery(save_path, battery, battery_size)) return -1;
    return 0;
}

}  // namespace

int run_fceumm(const std::string &rom_path, const FceummHost &host)
{
    std::unique_lock<std::mutex> lock(core_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return -1;
    try {
        return run_session(rom_path, host);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "fceumm: %s\n", error.what());
    } catch (...) {
        std::fprintf(stderr, "fceumm: unexpected core failure\n");
    }
    return -1;
}
