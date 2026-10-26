#pragma once

#include "gentest/harness_client.h"

#include <pthread.h>

namespace gentest::harness::detail {
struct Slot {
    std::uint32_t                         tag{}, size{};
    std::array<char, Channel::maxPayload> data{};
};
struct Queue {
    pthread_mutex_t                     mutex;
    std::uint32_t                       head{}, count{};
    bool                                closed{};
    std::array<Slot, Channel::capacity> slots;
};
struct Shared {
    std::uint32_t        magic{};
    std::array<Queue, 2> queues;
};
} // namespace gentest::harness::detail
