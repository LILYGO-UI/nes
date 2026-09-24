#ifndef LILYGO_UI_NES_PCM_RING_BUFFER_HPP
#define LILYGO_UI_NES_PCM_RING_BUFFER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

// A bounded PCM queue. Producers never wait for the audio device: when full,
// the oldest samples are discarded to keep playback close to the current frame.
class PcmRingBuffer {
public:
    explicit PcmRingBuffer(std::size_t capacity) : storage_(capacity) {}

    PcmRingBuffer(const PcmRingBuffer &)            = delete;
    PcmRingBuffer &operator=(const PcmRingBuffer &) = delete;

    void reset() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        read_  = 0;
        write_ = 0;
        size_  = 0;
    }

    // Returns the number of samples discarded, including samples too large to
    // fit in the queue.
    std::size_t push(const std::int16_t *samples, std::size_t count) noexcept
    {
        if (!samples || count == 0 || storage_.empty()) return count;

        std::lock_guard<std::mutex> lock(mutex_);
        std::size_t dropped = 0;
        if (count > storage_.size()) {
            const auto skip = count - storage_.size();
            samples += skip;
            count = storage_.size();
            dropped += skip;
        }
        if (size_ + count > storage_.size()) {
            const auto discard = size_ + count - storage_.size();
            read_ = (read_ + discard) % storage_.size();
            size_ -= discard;
            dropped += discard;
        }
        const auto first = std::min(count, storage_.size() - write_);
        std::copy_n(samples, first, storage_.data() + write_);
        std::copy_n(samples + first, count - first, storage_.data());
        write_ = (write_ + count) % storage_.size();
        size_ += count;
        return dropped;
    }

    std::size_t pop(std::int16_t *destination, std::size_t count) noexcept
    {
        if (!destination || count == 0) return 0;

        std::lock_guard<std::mutex> lock(mutex_);
        const auto available = count < size_ ? count : size_;
        if (available > 0) {
            const auto first = std::min(available, storage_.size() - read_);
            std::copy_n(storage_.data() + read_, first, destination);
            std::copy_n(storage_.data(), available - first, destination + first);
            read_ = (read_ + available) % storage_.size();
        }
        size_ -= available;
        return available;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == 0;
    }

private:
    std::vector<std::int16_t> storage_;
    mutable std::mutex mutex_;
    std::size_t read_  = 0;
    std::size_t write_ = 0;
    std::size_t size_  = 0;
};

#endif
