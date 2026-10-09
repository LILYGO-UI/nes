#include "emulator/nes_emulator.hpp"
#include "emulator/fceumm_core.hpp"
#include "emulator/nes_performance.hpp"
#include "emulator/pcm_ring_buffer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <unistd.h>

#ifdef LILYGO_UI_NES_SDL_AUDIO
#include <SDL.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;
#endif

namespace {

// Use a rate supported by the CM0 ES8389 codec's 19.2 MHz master clock.
constexpr int kAudioRate       = 48000;
constexpr int kAudioRefresh    = 60;

std::mutex emulator_mutex;
std::thread emulator_thread;
std::atomic<bool> emulator_running{false};
std::atomic<bool> emulator_finished{false};
std::atomic<bool> stop_requested{false};
std::atomic<int> emulator_result{0};
std::string active_rom_path;

struct StateRequest {
    NesStateOperation operation;
    std::string path;
};
std::mutex state_mutex;
std::optional<StateRequest> state_request;
std::optional<NesStateResult> state_result;
std::atomic<bool> state_pending{false};
void reset_loaded_state_audio();

bool request_state(NesStateOperation operation, const std::string &path)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    if (path.empty() || !emulator_running.load() || stop_requested.load() ||
        state_pending.load() || state_result) return false;
    state_request = StateRequest{operation, path};
    state_pending.store(true);
    return true;
}

bool take_state_request(NesStateOperation &operation, std::string &path)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    if (!state_request) return false;
    operation = state_request->operation;
    path = std::move(state_request->path);
    state_request.reset();
    return true;
}

void complete_state_request(NesStateResult result)
{
    if (result.operation == NesStateOperation::load && result.success) reset_loaded_state_audio();
    std::lock_guard<std::mutex> lock(state_mutex);
    state_result = std::move(result);
    state_pending.store(false);
}

std::mutex frame_mutex;
std::array<std::uint16_t, NesEmulator::frame_pixels> frame_a{};
std::array<std::uint16_t, NesEmulator::frame_pixels> frame_b{};
std::array<std::uint16_t, NesEmulator::frame_pixels> *frame_front = &frame_a;
std::array<std::uint16_t, NesEmulator::frame_pixels> *frame_back  = &frame_b;
std::uint32_t frame_sequence                                      = 0;
std::uint32_t copied_sequence                                     = 0;

std::atomic<std::uint32_t> pressed_mask{0};
std::atomic<std::uint32_t> rapid_pressed_mask{0};
std::atomic<unsigned> rapid_phase{0};

std::mutex clock_mutex;
std::condition_variable clock_wakeup;
std::chrono::steady_clock::time_point frame_deadline;
std::chrono::nanoseconds frame_period{0};
bool clock_active = false;

#ifdef LILYGO_UI_NES_SDL_AUDIO
SDL_AudioDeviceID audio_device = 0;
bool audio_subsystem_owned     = false;
bool audio_started             = false;
#else
int audio_pipe = -1;
pid_t audio_player_pid = -1;
constexpr int kAudioMaxSamples = (kAudioRate + kAudioRefresh - 1) / kAudioRefresh;
constexpr std::size_t kAudioQueueSamples = static_cast<std::size_t>(kAudioRate) / 20;
PcmRingBuffer audio_queue(kAudioQueueSamples);
std::mutex audio_queue_mutex;
std::condition_variable audio_queue_wakeup;
std::thread audio_writer_thread;
std::atomic<bool> audio_writer_stop{false};
std::atomic<bool> audio_writer_alive{false};
#endif

void reset_frames()
{
    std::lock_guard<std::mutex> lock(frame_mutex);
    frame_a.fill(0);
    frame_b.fill(0);
    frame_front     = &frame_a;
    frame_back      = &frame_b;
    frame_sequence  = 1;
    copied_sequence = 0;
}

#ifndef LILYGO_UI_NES_SDL_AUDIO
void request_audio_stop()
{
    {
        std::lock_guard<std::mutex> lock(audio_queue_mutex);
        audio_writer_stop.store(true);
    }
    audio_queue_wakeup.notify_all();
}

void reap_audio_player()
{
    if (audio_player_pid <= 0) return;
    const auto player = audio_player_pid;
    audio_player_pid = -1;
    const auto initial_result = waitpid(player, nullptr, WNOHANG);
    if (initial_result == player || (initial_result < 0 && errno == ECHILD)) return;

    kill(player, SIGTERM);
    for (int retry = 0; retry < 20; ++retry) {
        const auto result = waitpid(player, nullptr, WNOHANG);
        if (result == player || (result < 0 && errno == ECHILD)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    kill(player, SIGKILL);
    while (waitpid(player, nullptr, 0) < 0 && errno == EINTR) {}
}

void audio_writer_main()
{
    // A disconnected player must disable sound, not terminate the application.
    sigset_t blocked_signals;
    sigemptyset(&blocked_signals);
    sigaddset(&blocked_signals, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &blocked_signals, nullptr);

    std::array<std::int16_t, kAudioMaxSamples> chunk{};
    while (!audio_writer_stop.load()) {
        {
            std::unique_lock<std::mutex> lock(audio_queue_mutex);
            audio_queue_wakeup.wait(lock, [] { return audio_writer_stop.load() || !audio_queue.empty(); });
            if (audio_writer_stop.load()) break;
        }
        const auto samples = audio_queue.pop(chunk.data(), chunk.size());
        std::size_t offset = 0;
        const auto bytes = samples * sizeof(std::int16_t);
        nes_performance::Scope audio_write(nes_performance::Stage::audio_write);
        while (offset < bytes && !audio_writer_stop.load()) {
            pollfd output{audio_pipe, POLLOUT, 0};
            const int ready = poll(&output, 1, 10);
            if (ready < 0 && errno == EINTR) continue;
            if (ready == 0) continue;
            if (ready < 0 || (output.revents & (POLLERR | POLLHUP | POLLNVAL))) {
                std::fprintf(stderr, "nes audio: aplay output disconnected\n");
                audio_writer_alive.store(false);
                return;
            }
            const auto written = write(audio_pipe, reinterpret_cast<const char *>(chunk.data()) + offset, bytes - offset);
            if (written < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            if (written <= 0) {
                std::fprintf(stderr, "nes audio: could not write samples to aplay: %s\n", std::strerror(errno));
                audio_writer_alive.store(false);
                return;
            }
            offset += static_cast<std::size_t>(written);
        }
        if (offset == bytes) nes_performance::record_audio_output(samples);
    }
    audio_writer_alive.store(false);
}
#endif

void close_audio()
{
#ifdef LILYGO_UI_NES_SDL_AUDIO
    if (audio_device) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }
    if (audio_subsystem_owned) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        audio_subsystem_owned = false;
    }
    audio_started = false;
#else
    request_audio_stop();
    if (audio_writer_thread.joinable()) audio_writer_thread.join();
    audio_queue.reset();
    if (audio_pipe >= 0) {
        close(audio_pipe);
        audio_pipe = -1;
    }
    reap_audio_player();
#endif
}

bool open_audio()
{
    if (const char *setting = std::getenv("NES_AUDIO"); setting && std::strcmp(setting, "0") == 0) return false;
#ifdef LILYGO_UI_NES_SDL_AUDIO
    if (audio_device) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "nes audio: could not initialize SDL audio: %s\n", SDL_GetError());
        return false;
    }
    audio_subsystem_owned = true;

    SDL_AudioSpec desired{};
    desired.freq     = kAudioRate;
    desired.format   = AUDIO_S16SYS;
    desired.channels = 1;
    desired.samples  = 512;
    audio_device     = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
    if (!audio_device) {
        std::fprintf(stderr, "nes audio: could not open the default output device: %s\n", SDL_GetError());
        close_audio();
        return false;
    }
    std::fprintf(stderr, "nes audio: SDL output ready (%d Hz, mono)\n", kAudioRate);
#else
    if (audio_pipe >= 0) {
        if (audio_writer_alive.load()) return true;
        close_audio();
    }

    const char *player = nullptr;
    if (const char *override_player = std::getenv("NES_AUDIO_PLAYER"); override_player && override_player[0]) {
        if (access(override_player, X_OK) == 0) player = override_player;
    } else if (access("/usr/bin/aplay", X_OK) == 0) {
        player = "/usr/bin/aplay";
    } else if (access("/bin/aplay", X_OK) == 0) {
        player = "/bin/aplay";
    }
    if (!player) {
        std::fprintf(stderr, "nes audio: aplay was not found; install alsa-utils to enable sound\n");
        return false;
    }

    int pipe_ends[2];
    if (pipe(pipe_ends) != 0) {
        std::fprintf(stderr, "nes audio: could not create aplay pipe: %s\n", std::strerror(errno));
        return false;
    }
    if (fcntl(pipe_ends[0], F_SETFD, FD_CLOEXEC) < 0 || fcntl(pipe_ends[1], F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(pipe_ends[1], F_SETFL, O_NONBLOCK) < 0) {
        std::fprintf(stderr, "nes audio: could not configure aplay pipe: %s\n", std::strerror(errno));
        close(pipe_ends[0]);
        close(pipe_ends[1]);
        return false;
    }
#ifdef F_SETPIPE_SZ
    // Keep the kernel pipe small as well as the userspace PCM queue.
    (void)fcntl(pipe_ends[1], F_SETPIPE_SZ, 4096);
#endif
    posix_spawn_file_actions_t actions;
    int spawn_error = posix_spawn_file_actions_init(&actions);
    if (spawn_error != 0) {
        close(pipe_ends[0]);
        close(pipe_ends[1]);
        return false;
    }
    spawn_error = posix_spawn_file_actions_adddup2(&actions, pipe_ends[0], STDIN_FILENO);
    if (!spawn_error && pipe_ends[0] != STDIN_FILENO) spawn_error = posix_spawn_file_actions_addclose(&actions, pipe_ends[0]);
    if (!spawn_error) spawn_error = posix_spawn_file_actions_addclose(&actions, pipe_ends[1]);
    const std::string rate = std::to_string(kAudioRate);
    const char *arguments[] = {player, "-q", "-D", "default", "-t", "raw", "-f", "S16_LE", "-c", "1",
                               "-r", rate.c_str(), "-B", "50000", "-F", "10000", "-", nullptr};
    if (!spawn_error) spawn_error = posix_spawn(&audio_player_pid, player, &actions, nullptr,
                                               const_cast<char **>(arguments), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipe_ends[0]);
    if (spawn_error != 0) {
        close(pipe_ends[1]);
        audio_player_pid = -1;
        std::fprintf(stderr, "nes audio: could not start aplay: %s\n", std::strerror(spawn_error));
        return false;
    }
    audio_pipe = pipe_ends[1];
    audio_queue.reset();
    audio_writer_stop.store(false);
    audio_writer_alive.store(true);
    try {
        audio_writer_thread = std::thread(audio_writer_main);
    } catch (...) {
        audio_writer_alive.store(false);
        close_audio();
        return false;
    }
#endif
    return true;
}

void submit_audio_samples(const std::int16_t *samples, std::size_t sample_count)
{
    if (!samples || sample_count == 0) return;
    nes_performance::record_audio_generated(sample_count);
#ifdef LILYGO_UI_NES_SDL_AUDIO
    if (!audio_device) return;
    const Uint32 max_queued_bytes = kAudioRate * sizeof(std::int16_t) / 10;
    const Uint32 startup_bytes    = 2 * kAudioRate * sizeof(std::int16_t) / kAudioRefresh;
    const auto bytes                  = static_cast<Uint32>(sample_count * sizeof(std::int16_t));
    // Discard stale audio after a stall so playback stays close to the current frame.
    if (SDL_GetQueuedAudioSize(audio_device) + bytes > max_queued_bytes) {
        SDL_PauseAudioDevice(audio_device, 1);
        SDL_ClearQueuedAudio(audio_device);
        audio_started = false;
    }
    int queue_result;
    {
        nes_performance::Scope audio_write(nes_performance::Stage::audio_write);
        queue_result = SDL_QueueAudio(audio_device, samples, bytes);
    }
    if (queue_result != 0) {
        std::fprintf(stderr, "nes audio: could not queue samples: %s\n", SDL_GetError());
        close_audio();
        return;
    }
    nes_performance::record_audio_output(sample_count);
    if (!audio_started && SDL_GetQueuedAudioSize(audio_device) >= startup_bytes) {
        SDL_PauseAudioDevice(audio_device, 0);
        audio_started = true;
    }
#else
    if (audio_pipe < 0 || !audio_writer_alive.load()) return;
    {
        std::lock_guard<std::mutex> lock(audio_queue_mutex);
        const auto dropped = audio_queue.push(samples, sample_count);
        nes_performance::record_audio_dropped(dropped);
    }
    audio_queue_wakeup.notify_one();
#endif
}

void stop_frame_clock()
{
    {
        std::lock_guard<std::mutex> lock(clock_mutex);
        clock_active = false;
    }
    clock_wakeup.notify_all();
}

void reset_loaded_state_audio()
{
#ifdef LILYGO_UI_NES_SDL_AUDIO
    if (audio_device) {
        SDL_PauseAudioDevice(audio_device, 1);
        SDL_ClearQueuedAudio(audio_device);
    }
    audio_started = false;
#else
    audio_queue.reset();
#endif
    std::lock_guard<std::mutex> lock(clock_mutex);
    if (clock_active) frame_deadline = std::chrono::steady_clock::now() + frame_period;
}

int install_frame_clock(double frequency)
{
    if (!std::isfinite(frequency) || frequency <= 0 || frequency > 1000000000) return -1;
    std::lock_guard<std::mutex> lock(clock_mutex);
    frame_period = std::chrono::nanoseconds(static_cast<std::int64_t>(1000000000.0 / frequency));
    frame_deadline = std::chrono::steady_clock::now() + frame_period;
    clock_active = true;
    return 0;
}

int wait_frame()
{
    std::unique_lock<std::mutex> lock(clock_mutex);
    if (clock_active && !stop_requested.load()) {
        clock_wakeup.wait_until(lock, frame_deadline, [] { return !clock_active || stop_requested.load(); });
    }
    if (!clock_active || stop_requested.load()) return 0;

    const auto now = std::chrono::steady_clock::now();
    if (now < frame_deadline) return 0;
    const auto elapsed = (now - frame_deadline) / frame_period + 1;
    // Advance from the existing deadline so rendering time cannot accumulate as drift.
    frame_deadline += frame_period * elapsed;
    return static_cast<int>(std::min<std::int64_t>(elapsed, std::numeric_limits<int>::max()));
}

void publish_frame(const std::uint16_t *pixels, std::size_t count)
{
    if (!pixels || count != NesEmulator::frame_pixels) return;
    std::lock_guard<std::mutex> lock(frame_mutex);
    std::copy_n(pixels, count, frame_back->begin());
    std::swap(frame_front, frame_back);
    ++frame_sequence;
}

void emulator_thread_main()
{
    int result = -1;
    try {
        open_audio();
        FceummHost host;
        host.sample_rate = kAudioRate;
        host.stop_requested = [] { return stop_requested.load(); };
        host.buttons = [] {
            const auto regular = pressed_mask.load();
            const auto rapid   = rapid_pressed_mask.load();
            // The core polls once per frame.  Alternating the rapid mask makes
            // C/D behave like the native turbo A/B inputs while still allowing
            // a regular A/B binding to remain continuously pressed.
            const auto pulse = rapid && ((rapid_phase.fetch_xor(1U) & 1U) == 0) ? rapid : 0U;
            return regular | pulse;
        };
        host.wait_frame = wait_frame;
        host.set_frame_rate = [](double frequency) { install_frame_clock(frequency); };
        host.publish_frame = publish_frame;
        host.submit_audio = submit_audio_samples;
        host.take_state_request = take_state_request;
        host.state_completed = complete_state_request;
        result = run_fceumm(active_rom_path, host);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "nes: emulator failed: %s\n", error.what());
    }
    close_audio();
    stop_frame_clock();
    emulator_result.store(result);
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        emulator_running.store(false);
        if (state_request) {
            state_result = NesStateResult{state_request->operation, false, "Game stopped before the state operation completed"};
            state_request.reset();
        }
        state_pending.store(false);
    }
    emulator_finished.store(true);
}

}  // namespace

NesEmulator::~NesEmulator()
{
    stop();
    if (emulator_thread.joinable()) emulator_thread.join();
    stop_frame_clock();
    close_audio();
}

bool NesEmulator::start(const std::string &rom_path)
{
    std::lock_guard<std::mutex> lock(emulator_mutex);
    if (emulator_running.load() || rom_path.empty()) return false;
    if (emulator_thread.joinable()) emulator_thread.join();

    nes_performance::start("fceumm");
    std::fprintf(stderr, "nes: core=fceumm\n");

    active_rom_path = rom_path;
    reset_frames();
    pressed_mask.store(0);
    rapid_pressed_mask.store(0);
    rapid_phase.store(0);
    stop_requested.store(false);
    emulator_finished.store(false);
    emulator_result.store(0);
    {
        std::lock_guard<std::mutex> state_lock(state_mutex);
        state_request.reset();
        state_result.reset();
        state_pending.store(false);
    }
    emulator_running.store(true);
    try {
        emulator_thread = std::thread(emulator_thread_main);
    } catch (...) {
        emulator_running.store(false);
        return false;
    }
    return true;
}

void NesEmulator::stop() noexcept
{
    {
        std::lock_guard<std::mutex> lock(clock_mutex);
        if (emulator_running.load()) stop_requested.store(true);
    }
    clock_wakeup.notify_all();
#ifndef LILYGO_UI_NES_SDL_AUDIO
    request_audio_stop();
#endif
}

void NesEmulator::set_button(NesButton button, bool pressed) noexcept
{
    const auto bit = static_cast<unsigned>(button);
    if (bit >= 8) return;
    const std::uint32_t mask = 1U << bit;
    if (pressed) {
        pressed_mask.fetch_or(mask);
    } else {
        pressed_mask.fetch_and(~mask);
    }
}

void NesEmulator::set_rapid_button(NesButton button, bool pressed) noexcept
{
    const auto bit = static_cast<unsigned>(button);
    // Turbo is only defined for the NES A/B action buttons.  Rejecting other
    // bits keeps accidental direction or system-button turbo from leaking to
    // the core input mask.
    if (button != NesButton::a && button != NesButton::b) return;
    const std::uint32_t mask = 1U << bit;
    if (pressed) {
        const auto previous = rapid_pressed_mask.fetch_or(mask);
        if (previous == 0) rapid_phase.store(0);
    } else {
        rapid_pressed_mask.fetch_and(~mask);
    }
}

bool NesEmulator::running() const noexcept
{
    return emulator_running.load();
}

bool NesEmulator::request_save_state(const std::string &path)
{
    return request_state(NesStateOperation::save, path);
}

bool NesEmulator::request_load_state(const std::string &path)
{
    return request_state(NesStateOperation::load, path);
}

bool NesEmulator::consume_state_result(NesStateResult &result)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    if (!state_result) return false;
    result = std::move(*state_result);
    state_result.reset();
    return true;
}

bool NesEmulator::state_operation_pending() const noexcept
{
    return state_pending.load();
}

bool NesEmulator::consume_finished(int &result) noexcept
{
    if (!emulator_finished.exchange(false)) return false;
    if (emulator_thread.joinable()) emulator_thread.join();
    result = emulator_result.load();
    return true;
}

bool NesEmulator::copy_latest_frame(std::uint16_t *destination, std::size_t pixels) noexcept
{
    if (!destination || pixels < frame_pixels) return false;
    std::lock_guard<std::mutex> lock(frame_mutex);
    if (copied_sequence == frame_sequence) return false;
    nes_performance::Scope copy_time(nes_performance::Stage::frame_copy);
    std::copy(frame_front->cbegin(), frame_front->cend(), destination);
    copied_sequence = frame_sequence;
    return true;
}
