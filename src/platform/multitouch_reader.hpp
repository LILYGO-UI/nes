#ifndef LILYGO_UI_NES_PLATFORM_MULTITOUCH_READER_HPP
#define LILYGO_UI_NES_PLATFORM_MULTITOUCH_READER_HPP

#include <array>
#include <cstddef>
#include <memory>

struct TouchPoint {
    bool active = false;
    int x       = -1;
    int y       = -1;
};

class MultitouchReader {
public:
    static constexpr std::size_t maximum_points = 10;

    MultitouchReader();
    ~MultitouchReader();

    MultitouchReader(const MultitouchReader &)            = delete;
    MultitouchReader &operator=(const MultitouchReader &) = delete;

    [[nodiscard]] static TouchPoint map_point(int raw_x, int raw_y, bool active,
                                              int raw_max_x, int raw_max_y,
                                              int display_width, int display_height) noexcept;
    bool start(int display_width, int display_height);
    void set_display_size(int display_width, int display_height) noexcept;
    void stop() noexcept;
    [[nodiscard]] std::array<TouchPoint, maximum_points> points() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
