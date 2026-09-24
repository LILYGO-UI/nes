#include "platform/multitouch_reader.hpp"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

struct MultitouchReader::Impl {
    std::atomic<bool> running{false};
    std::atomic<int> display_width{568};
    std::atomic<int> display_height{1232};
    mutable std::mutex mutex;
    std::thread thread;
    std::array<TouchPoint, maximum_points> points{};

#if defined(__linux__)
    int raw_max_x = 1060;
    int raw_max_y = 2400;

    TouchPoint map(int raw_x, int raw_y, bool active) const noexcept
    {
        TouchPoint point;
        point.active = active && raw_x >= 0 && raw_y >= 0;
        if (!point.active) return point;

        const int width  = display_width.load();
        const int height = display_height.load();
        if (width > height) {
            point.x = raw_y * (width - 1) / raw_max_y;
            point.y = (raw_max_x - raw_x) * (height - 1) / raw_max_x;
        } else {
            point.x = raw_x * (width - 1) / raw_max_x;
            point.y = raw_y * (height - 1) / raw_max_y;
        }
        return point;
    }

    void run(const std::string &path)
    {
        const int descriptor = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (descriptor < 0) {
            running.store(false);
            return;
        }

        input_absinfo info{};
        if (ioctl(descriptor, EVIOCGABS(ABS_MT_POSITION_X), &info) == 0 && info.maximum > 0) raw_max_x = info.maximum;
        if (ioctl(descriptor, EVIOCGABS(ABS_MT_POSITION_Y), &info) == 0 && info.maximum > 0) raw_max_y = info.maximum;

        struct RawPoint {
            bool active = false;
            int x       = -1;
            int y       = -1;
        };
        std::array<RawPoint, maximum_points> raw{};
        int slot                 = 0;
        bool single_touch_active = false;

        while (running.load()) {
            input_event event{};
            const auto bytes = read(descriptor, &event, sizeof(event));
            if (bytes != static_cast<ssize_t>(sizeof(event))) {
                if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                    usleep(2000);
                    continue;
                }
                break;
            }

            if (event.type == EV_ABS) {
                if (event.code == ABS_MT_SLOT) {
                    slot = event.value < 0 ? 0 : event.value;
                    if (slot >= static_cast<int>(maximum_points)) slot = static_cast<int>(maximum_points) - 1;
                } else if (event.code == ABS_MT_TRACKING_ID) {
                    raw[static_cast<std::size_t>(slot)].active = event.value >= 0;
                    if (event.value < 0) {
                        raw[static_cast<std::size_t>(slot)].x = -1;
                        raw[static_cast<std::size_t>(slot)].y = -1;
                    }
                } else if (event.code == ABS_MT_POSITION_X) {
                    raw[static_cast<std::size_t>(slot)].x = event.value;
                } else if (event.code == ABS_MT_POSITION_Y) {
                    raw[static_cast<std::size_t>(slot)].y = event.value;
                } else if (event.code == ABS_X) {
                    raw[0].x = event.value;
                } else if (event.code == ABS_Y) {
                    raw[0].y = event.value;
                }
            } else if (event.type == EV_KEY && event.code == BTN_TOUCH) {
                single_touch_active = event.value != 0;
                raw[0].active       = single_touch_active;
            } else if (event.type == EV_SYN && event.code == SYN_REPORT) {
                std::array<TouchPoint, maximum_points> mapped{};
                for (std::size_t index = 0; index < raw.size(); ++index) {
                    const bool active = raw[index].active || (index == 0 && single_touch_active);
                    mapped[index]     = map(raw[index].x, raw[index].y, active);
                }
                std::lock_guard<std::mutex> lock(mutex);
                points = mapped;
            }
        }

        close(descriptor);
        running.store(false);
    }
#endif
};

MultitouchReader::MultitouchReader() : impl_(std::make_unique<Impl>())
{
}

MultitouchReader::~MultitouchReader()
{
    stop();
}

bool MultitouchReader::start(int display_width, int display_height)
{
    set_display_size(display_width, display_height);
#if defined(__linux__)
    if (impl_->running.exchange(true)) return true;
    const char *configured = std::getenv("NES_TOUCH_DEVICE");
    const std::string path =
        configured && configured[0] ? configured : "/dev/input/by-path/platform-3f205000.i2c-event";
    if (access(path.c_str(), R_OK) != 0) {
        impl_->running.store(false);
        return false;
    }
    try {
        impl_->thread = std::thread([this, path] { impl_->run(path); });
    } catch (...) {
        impl_->running.store(false);
        return false;
    }
    return true;
#else
    return false;
#endif
}

void MultitouchReader::set_display_size(int display_width, int display_height) noexcept
{
    if (display_width > 0) impl_->display_width.store(display_width);
    if (display_height > 0) impl_->display_height.store(display_height);
}

void MultitouchReader::stop() noexcept
{
    impl_->running.store(false);
    if (impl_->thread.joinable()) impl_->thread.join();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->points = {};
}

std::array<TouchPoint, MultitouchReader::maximum_points> MultitouchReader::points() const noexcept
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->points;
}
