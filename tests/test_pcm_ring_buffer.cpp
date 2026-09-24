#include "emulator/pcm_ring_buffer.hpp"

#include <cassert>
#include <atomic>
#include <cstdint>
#include <thread>

int main()
{
    PcmRingBuffer queue(4);
    const std::int16_t first[] = {1, 2, 3, 4};
    assert(queue.push(first, 4) == 0);
    std::int16_t output[4]{};
    assert(queue.pop(output, 2) == 2);
    assert(output[0] == 1 && output[1] == 2);

    const std::int16_t second[] = {5, 6, 7, 8};
    assert(queue.push(second, 4) == 2);
    assert(queue.pop(output, 4) == 4);
    assert(output[0] == 5 && output[1] == 6 && output[2] == 7 && output[3] == 8);

    const std::int16_t oversized[] = {9, 10, 11, 12, 13};
    assert(queue.push(oversized, 5) == 1);
    assert(queue.pop(output, 4) == 4);
    assert(output[0] == 10 && output[3] == 13);
    assert(queue.empty());

    assert(queue.push(first, 3) == 0);
    queue.reset();
    assert(queue.empty());
    assert(queue.pop(output, 4) == 0);

    PcmRingBuffer concurrent(32);
    std::atomic<bool> finished{false};
    std::thread producer([&] {
        for (std::int16_t value = 1; value <= 20000; ++value) assert(concurrent.push(&value, 1) <= 1);
        finished.store(true);
    });
    std::int16_t last = 0;
    do {
        const auto count = concurrent.pop(output, 4);
        for (std::size_t index = 0; index < count; ++index) {
            assert(output[index] > last);
            last = output[index];
        }
    } while (!finished.load() || !concurrent.empty());
    producer.join();
    assert(last == 20000);
    return 0;
}
