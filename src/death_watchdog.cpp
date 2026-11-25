#include "death_watchdog.h"

#include "notification_forward.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <fstream>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Shell declarations use Win32 types from windows.h.
#include <shellapi.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace gentest::detail {
namespace {
constexpr std::size_t slots = 256;
struct Slot {
    std::uint32_t                                  size;
    std::array<std::byte, notificationRecordLimit> data;
};

struct Channel {
    std::uint32_t           consumed;
    std::uint32_t           failed;
    std::uint32_t           attached;
    std::array<Slot, slots> records;
};

// Only fixed-width little-endian fields cross the mapping (including under a cross emulator).
static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free);
static_assert(alignof(std::uint32_t) >= std::atomic_ref<std::uint32_t>::required_alignment);
auto little(std::uint32_t n) noexcept -> std::uint32_t {
    if constexpr (std::endian::native == std::endian::big) {
        return (n >> 24U) | ((n >> 8U) & 0xff00U) | ((n << 8U) & 0xff0000U) | (n << 24U);
    }
    return n;
}
auto load(std::uint32_t &field) noexcept -> std::uint32_t { return little(std::atomic_ref(field).load(std::memory_order_acquire)); }
void store(std::uint32_t &field, std::uint32_t value) noexcept { std::atomic_ref(field).store(little(value), std::memory_order_release); }

class Mapping {
  public:
    explicit Mapping(const std::filesystem::path &path, bool create) {
        if (create) {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file.exceptions(std::ios::badbit | std::ios::failbit);
            file.seekp(sizeof(Channel) - 1).put('\0');
            file.close();
        }
#if defined(_WIN32)
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            throw std::system_error(GetLastError(), std::system_category(), "open watchdog file");
        HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, 0, nullptr);
        auto   error   = GetLastError();
        CloseHandle(file);
        if (mapping == nullptr)
            throw std::system_error(error, std::system_category(), "map watchdog file");
        data  = static_cast<Channel *>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Channel)));
        error = GetLastError();
        CloseHandle(mapping);
        if (!data)
            throw std::system_error(error, std::system_category(), "map watchdog view");
#else
        int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd == -1)
            throw std::system_error(errno, std::generic_category(), "open watchdog file");
        struct stat info{};
        if (::fstat(fd, &info) != 0 || info.st_size != sizeof(Channel)) {
            ::close(fd);
            throw std::runtime_error("invalid watchdog file");
        }
        void *mapping = ::mmap(nullptr, sizeof(Channel), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        int   error   = errno;
        ::close(fd);
        if (mapping == MAP_FAILED)
            throw std::system_error(error, std::generic_category(), "map watchdog file");
        data = static_cast<Channel *>(mapping);
#endif
        // Initialize the shared fields before launching the child. The child maps
        // the same layout without resetting records the parent may be reading.
        if (create)
            ::new (data) Channel{};
        else
            ::new (data) Channel;
    }

    Mapping(const Mapping &)                     = delete;
    auto operator=(const Mapping &) -> Mapping & = delete;
    ~Mapping() {
#if defined(_WIN32)
        UnmapViewOfFile(data);
#else
        ::munmap(data, sizeof(Channel));
#endif
    }

    Channel *data{};
};

class Sink final : public NotificationSink {
  public:
    Sink(const std::filesystem::path &path, Notifier upstream) : mapping_(path, false), upstream_(std::move(upstream)) {
        if (std::atomic_ref(mapping_.data->attached).exchange(little(1)) != 0)
            throw std::runtime_error("watchdog already attached");
    }
    void onError(NotifyResult error) noexcept override {
        store(mapping_.data->failed, 1);
        NotificationForwarder::onError(upstream_, error);
    }
    auto submit(const Notification &event) noexcept -> NotifyResult override {
#if !defined(_WIN32)
        // A fork inherits the mapping but has a private copy of the reservation counter.
        if (::getpid() != creator_) {
            onError(NotifyResult::transport_error);
            return NotifyResult::transport_error;
        }
#endif
        if (upstream_.active()) {
            const auto result = NotificationForwarder::submit(upstream_.forSubject(event.subject), event);
            if (result != NotifyResult::accepted) {
                onError(result);
                return result;
            }
        }
        // Custom records carry application data and never renew the watchdog.
        if (event.kind == NotificationKind::custom)
            return NotifyResult::accepted;
        auto &channel = *mapping_.data;
        auto  pos     = next_.load(std::memory_order_relaxed);
        do {
            if (static_cast<std::uint32_t>(pos) - load(channel.consumed) >= slots) {
                // A concurrent publisher may have advanced our stale position.
                const auto latest = next_.load(std::memory_order_relaxed);
                if (latest != pos) {
                    pos = latest;
                    continue;
                }
                onError(NotifyResult::queue_overflow);
                return NotifyResult::queue_overflow;
            }
            if (next_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                break;
        } while (true);

        auto record     = event;
        record.sequence = pos + 1;
        EncodedNotification encoded;
        auto                result = encodeNotification(record, encoded);
        if (result != NotifyResult::accepted) {
            onError(result);
            return result;
        }
        auto &slot = channel.records[pos % slots];
        std::memcpy(slot.data.data(), encoded.bytes.data(), encoded.size);
        store(slot.size, static_cast<std::uint32_t>(encoded.size));
        return NotifyResult::accepted;
    }

  private:
    Mapping                    mapping_;
    Notifier                   upstream_;
    std::atomic<std::uint64_t> next_{};
#if !defined(_WIN32)
    pid_t creator_ = ::getpid();
#endif
};
} // namespace

struct DeathWatchdog::State {
    Mapping                   mapping;
    WatchdogModel             model;
    std::chrono::milliseconds interval;
    WatchdogModel::TimePoint  idleSince = WatchdogModel::Clock::now();
    std::uint32_t             consumed{};
    State(const std::filesystem::path &path, std::chrono::milliseconds inactivity)
        : mapping(path, true), model({.inactivityTimeout = inactivity}), interval(inactivity) {}
};

DeathWatchdog::DeathWatchdog(const std::filesystem::path &path, std::chrono::milliseconds inactivity)
    : state_(std::make_unique<State>(path, inactivity)) {}
DeathWatchdog::~DeathWatchdog() = default;
bool DeathWatchdog::attached() const noexcept { return load(state_->mapping.data->attached) != 0; }

void DeathWatchdog::check() {
    auto      &s       = *state_;
    const auto now     = WatchdogModel::Clock::now();
    auto      &channel = *s.mapping.data;
    if (load(channel.failed))
        throw std::runtime_error("watchdog notification failed");
    // Startup and gaps between subjects also have an inactivity deadline.
    if (s.model.activeSubjects() == 0 && now - s.idleSince >= s.interval)
        throw std::runtime_error("watchdog inactivity timeout");
    for (std::size_t i = 0; i < slots; ++i) {
        auto      &slot = channel.records[s.consumed % slots];
        const auto size = load(slot.size);
        if (size == 0)
            break;
        Notification event;
        if (size > slot.data.size() || decodeNotification(std::span(slot.data).first(size), event) != NotifyResult::accepted)
            throw std::runtime_error("invalid watchdog notification");
        if (s.model.accept(event, now) == WatchdogResult::failed)
            break;
        store(slot.size, 0);
        store(channel.consumed, ++s.consumed);
        s.idleSince = now;
    }
    (void)s.model.advance(now);
    if (const auto &failure = s.model.failure()) {
        const auto &detail = failure->detail;
        throw std::runtime_error("watchdog: " + (detail.empty() ? "invalid notification" : detail) + " " + failure->label);
    }
}

void DeathWatchdog::finish() {
    check();
    // All writers have exited. A hole followed by a published record means a
    // producer died mid-publication and later events could not be validated.
    auto &records = state_->mapping.data->records;
    if (std::ranges::any_of(records, [](Slot &slot) { return load(slot.size) != 0; }))
        throw std::runtime_error("incomplete watchdog notifications");
}

#if defined(_WIN32)
auto wideProcessArguments(int first) -> std::vector<std::string> {
    int   count{};
    auto *wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide)
        throw std::runtime_error("cannot read command line");
    struct FreeArguments {
        LPWSTR *value;
        ~FreeArguments() { LocalFree(value); }
    } guard{wide};
    std::vector<std::string> args;
    for (int i = first; i < count; ++i) {
        const auto value = std::filesystem::path(wide[i]).u8string();
        args.emplace_back(value.begin(), value.end());
    }
    return args;
}
#endif

Notifier deathWatchdogNotifier(const std::filesystem::path &path, const Notifier &upstream) {
    return {std::make_shared<Sink>(path, upstream), upstream.active() ? upstream.producer() : ProducerIdentity{.id = 1, .generation = 1},
            upstream.active() ? upstream.subject() : 1};
}
} // namespace gentest::detail
