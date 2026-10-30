#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>

namespace gentest::harness {
using Deadline = std::chrono::steady_clock::time_point;

struct Message {
    std::uint32_t tag{};
    std::string   payload;
};

// Normal end-of-stream after every queued message has been received.
class ChannelClosed : public std::runtime_error {
  public:
    ChannelClosed() : std::runtime_error("shared-memory channel closed") {}
};

// Same-build Linux IPC. Descriptors, never filesystem names, identify a channel.
class Channel {
  public:
    static constexpr std::size_t capacity   = 16;
    static constexpr std::size_t maxPayload = 4096;
    using Descriptors                       = std::array<int, 3>;

    Channel();
    ~Channel();
    Channel(Channel &&) noexcept;
    auto operator=(Channel &&) noexcept -> Channel &;
    Channel(const Channel &)                     = delete;
    auto operator=(const Channel &) -> Channel & = delete;

    static auto create() -> Channel;
    static auto attach(Descriptors descriptors, bool child = true) -> Channel;
    static auto fromEnvironment() -> Channel;

    // Full queues and oversized messages are errors; successful sends own a copy.
    void send(const Message &message);

    // Does not wait for message arrival. Queue access can wait up to the one-second mutex deadline;
    // An empty closed queue throws ChannelClosed; lock and corruption errors remain distinct.
    auto tryReceive() -> std::optional<Message>;
    auto receive(Deadline deadline, const std::stop_token &stop = {}) -> Message;
    void close() noexcept;
    auto descriptors() const -> Descriptors;
    auto notificationFd() const -> int;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gentest::harness
