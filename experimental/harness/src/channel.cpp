#include "channel_layout.h"
#include "common.h"
#include "gentest/harness_client.h"

#include <charconv>
#include <climits>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>

namespace gentest::harness {
namespace {
using detail::Fd;
using detail::Queue;
using detail::Shared;
using detail::systemCheck;
constexpr std::uint32_t magic = 0x47544831;

void notify(int fd) noexcept {
    const std::uint64_t one = 1;
    while (::write(fd, &one, sizeof(one)) < 0 && errno == EINTR) {}
}

class Lock {
  public:
    explicit Lock(Queue &queue) : queue_(queue) {
        // A stopped live holder must not hang the test runner forever.
        timespec limit{};
        ::clock_gettime(CLOCK_MONOTONIC, &limit);
        ++limit.tv_sec;
        const int error = ::pthread_mutex_clocklock(&queue_.mutex, CLOCK_MONOTONIC, &limit);
        if (error == EOWNERDEAD) {
            queue_.closed = true;
            queue_.count  = 0;
            ::pthread_mutex_consistent(&queue_.mutex);
            ::pthread_mutex_unlock(&queue_.mutex);
            throw std::runtime_error("shared-memory channel owner died");
        }
        if (error) {
            throw std::system_error(error, std::generic_category(), "shared-memory channel lock");
        }
    }
    ~Lock() { ::pthread_mutex_unlock(&queue_.mutex); }
    Lock(const Lock &)                     = delete;
    auto operator=(const Lock &) -> Lock & = delete;

  private:
    Queue &queue_;
};
} // namespace

struct Channel::Impl {
    Fd                memory;
    std::array<Fd, 2> events;
    Shared           *shared{};
    unsigned          side{};
    ~Impl() {
        if (shared) {
            ::munmap(shared, sizeof(Shared));
        }
    }
};

Channel::Channel()                                        = default;
Channel::~Channel()                                       = default;
Channel::Channel(Channel &&) noexcept                     = default;
auto Channel::operator=(Channel &&) noexcept -> Channel & = default;

auto Channel::create() -> Channel {
    Channel channel;
    channel.impl_ = std::make_unique<Impl>();
    auto &impl    = *channel.impl_;
    impl.memory.reset(::memfd_create("gentest-channel", MFD_CLOEXEC | MFD_ALLOW_SEALING));
    systemCheck(impl.memory.value >= 0, "memfd_create");
    systemCheck(::ftruncate(impl.memory.value, sizeof(Shared)) == 0, "ftruncate channel");
    void *mapping = ::mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, impl.memory.value, 0);
    systemCheck(mapping != MAP_FAILED, "mmap channel");
    impl.shared = new (mapping) Shared{};

    pthread_mutexattr_t attributes;
    int                 error = ::pthread_mutexattr_init(&attributes);
    if (error) {
        throw std::system_error(error, std::generic_category(), "mutex attributes");
    }
    error = ::pthread_mutexattr_setpshared(&attributes, PTHREAD_PROCESS_SHARED);
    if (!error) {
        error = ::pthread_mutexattr_setrobust(&attributes, PTHREAD_MUTEX_ROBUST);
    }
    for (auto &queue : impl.shared->queues) {
        if (!error) {
            error = ::pthread_mutex_init(&queue.mutex, &attributes);
        }
    }
    ::pthread_mutexattr_destroy(&attributes);
    if (error) {
        throw std::system_error(error, std::generic_category(), "process-shared mutex");
    }
    for (auto &event : impl.events) {
        event.reset(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
        systemCheck(event.value >= 0, "eventfd");
    }
    impl.shared->magic = magic;
    systemCheck(::fcntl(impl.memory.value, F_ADD_SEALS, F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) == 0, "seal channel size");
    return channel;
}

auto Channel::attach(Descriptors descriptors, bool child) -> Channel {
    Channel channel;
    channel.impl_ = std::make_unique<Impl>();
    auto &impl    = *channel.impl_;
    impl.side     = child ? 1U : 0U;
    impl.memory.reset(::fcntl(descriptors[0], F_DUPFD_CLOEXEC, 3));
    systemCheck(impl.memory.value >= 0, "duplicate channel memory");
    for (unsigned i = 0; i != 2; ++i) {
        impl.events[i].reset(::fcntl(descriptors[i + 1], F_DUPFD_CLOEXEC, 3));
        systemCheck(impl.events[i].value >= 0, "duplicate channel event");
    }
    struct stat info{};
    systemCheck(::fstat(impl.memory.value, &info) == 0, "stat channel");
    if (info.st_size != sizeof(Shared)) {
        throw std::runtime_error("incompatible shared-memory channel size");
    }
    void *mapping = ::mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, impl.memory.value, 0);
    systemCheck(mapping != MAP_FAILED, "map inherited channel");
    impl.shared = static_cast<Shared *>(mapping);
    if (impl.shared->magic != magic) {
        throw std::runtime_error("incompatible shared-memory channel version");
    }
    return channel;
}

auto Channel::fromEnvironment() -> Channel {
    const char *raw = std::getenv("GENTEST_HARNESS_CHANNEL");
    if (!raw) {
        throw std::runtime_error("GENTEST_HARNESS_CHANNEL is missing");
    }
    std::string_view value(raw);
    Descriptors      descriptors{};
    for (unsigned i = 0; i != descriptors.size(); ++i) {
        const auto end    = value.find(',');
        const auto part   = value.substr(0, end);
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), descriptors[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size() || descriptors[i] < 3 ||
            (i != 2 && end == std::string_view::npos) || (i == 2 && end != std::string_view::npos)) {
            throw std::runtime_error("invalid inherited channel descriptors");
        }
        if (end != std::string_view::npos) {
            value.remove_prefix(end + 1);
        }
    }
    return attach(descriptors);
}

void Channel::send(const Message &message) {
    if (message.payload.size() > maxPayload) {
        throw std::length_error("shared-memory message exceeds 4096 bytes");
    }
    auto &queue = impl_->shared->queues[impl_->side];
    {
        Lock lock(queue);
        if (queue.closed) {
            throw std::runtime_error("shared-memory channel closed");
        }
        if (queue.count == capacity) {
            throw std::runtime_error("shared-memory channel full");
        }
        auto &slot = queue.slots[(queue.head + queue.count) % capacity];
        slot.tag   = message.tag;
        slot.size  = static_cast<std::uint32_t>(message.payload.size());
        std::memcpy(slot.data.data(), message.payload.data(), slot.size);
        ++queue.count;
    }
    notify(impl_->events[impl_->side].value);
}

auto Channel::tryReceive() -> std::optional<Message> {
    const unsigned incoming = 1U - impl_->side;
    auto          &queue    = impl_->shared->queues[incoming];
    Lock           lock(queue);
    std::uint64_t  signals{};
    while (::read(impl_->events[incoming].value, &signals, sizeof(signals)) < 0 && errno == EINTR) {}
    if (!queue.count) {
        if (queue.closed) {
            throw std::runtime_error("shared-memory channel closed");
        }
        return std::nullopt;
    }
    const auto &slot = queue.slots[queue.head];
    if (slot.size > maxPayload) {
        throw std::runtime_error("corrupt shared-memory message");
    }
    Message result{.tag = slot.tag, .payload = std::string(slot.data.data(), slot.size)};
    queue.head = (queue.head + 1) % capacity;
    --queue.count;
    if (queue.count || queue.closed) {
        notify(impl_->events[incoming].value);
    }
    return result;
}

auto Channel::receive(Deadline deadline, const std::stop_token &stop) -> Message {
    Fd cancelled(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
    systemCheck(cancelled.value >= 0, "cancellation eventfd");
    std::stop_callback callback(stop, [&] { notify(cancelled.value); });
    for (;;) {
        if (stop.stop_requested()) {
            throw std::runtime_error("channel receive cancelled");
        }
        if (auto message = tryReceive()) {
            return std::move(*message);
        }
        const auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            throw std::runtime_error("channel receive deadline expired");
        }
        std::array<pollfd, 2> descriptors{
            {{.fd = notificationFd(), .events = POLLIN, .revents = 0}, {.fd = cancelled.value, .events = POLLIN, .revents = 0}}};
        const int result = ::poll(descriptors.data(), descriptors.size(), static_cast<int>(std::min<std::int64_t>(left, INT_MAX)));
        systemCheck(result >= 0 || errno == EINTR, "poll channel");
    }
}

void Channel::close() noexcept {
    if (!impl_) {
        return;
    }
    for (unsigned i = 0; i != 2; ++i) {
        try {
            Lock lock(impl_->shared->queues[i]);
            impl_->shared->queues[i].closed = true;
        } catch (...) {
            // Cleanup still wakes the peer; its next access reports lock/owner failure.
            notify(impl_->events[i].value);
            continue;
        }
        notify(impl_->events[i].value);
    }
}
auto Channel::descriptors() const -> Descriptors { return {impl_->memory.value, impl_->events[0].value, impl_->events[1].value}; }
auto Channel::notificationFd() const -> int { return impl_->events[1U - impl_->side].value; }
} // namespace gentest::harness
