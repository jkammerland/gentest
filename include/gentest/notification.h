#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gentest {

inline constexpr std::uint16_t notificationProtocolVersion = 1;
inline constexpr std::size_t   notificationRecordLimit     = 4096;

struct NotificationLimits {
    std::size_t queueCapacity   = 256;
    std::size_t maximumSubjects = 256;
};

struct ProducerIdentity {
    std::uint64_t id         = 0;
    std::uint64_t generation = 0;

    friend auto operator==(const ProducerIdentity &, const ProducerIdentity &) -> bool = default;
};

enum class NotificationKind : std::uint8_t {
    subject_started   = 1,
    progress          = 2,
    custom            = 3,
    silence_allowance = 4,
    subject_finished  = 5,
};

enum class NotifyResult {
    accepted,
    disabled,
    invalid_record,
    unsupported_version,
    record_too_large,
    queue_overflow,
    transport_error,
    closed,
    resource_exhausted,
};

// Payload views are valid only during submit/observer calls or while the decoded
// wire buffer remains alive. Sinks must copy the data before accepting a record.
struct Notification {
    ProducerIdentity         producer{};
    std::uint64_t            subject  = 0;
    std::uint64_t            sequence = 0;
    NotificationKind         kind     = NotificationKind::custom;
    std::chrono::nanoseconds allowance{};
    std::string_view         topic{};
    std::string_view         payload{};
};

struct EncodedNotification {
    std::array<std::byte, notificationRecordLimit> bytes{};
    std::size_t                                    size = 0;

    [[nodiscard]] auto view() const noexcept -> std::span<const std::byte> { return {bytes.data(), size}; }
};

// Version 1 is little endian: version:u16, kind:u8, reserved:u8, size:u32,
// producer:u64, generation:u64, subject:u64, sequence:u64, allowance_ns:u64,
// topic_size:u16, payload_size:u16, followed by exact topic and payload bytes.
// No native structs, pointers, timestamps, or terminating NULs are transmitted.
[[nodiscard]] auto encodeNotification(const Notification &notification, EncodedNotification &encoded) noexcept -> NotifyResult;
[[nodiscard]] auto decodeNotification(std::span<const std::byte> encoded, Notification &notification) noexcept -> NotifyResult;

class NotificationSink {
  public:
    virtual ~NotificationSink() = default;

    // May be called concurrently. Admission must be bounded and nonblocking.
    // Input sequence is zero: the sink assigns a strictly increasing sequence
    // per producer incarnation when serializing accepted records for delivery.
    [[nodiscard]] virtual auto submit(const Notification &notification) noexcept -> NotifyResult = 0;

    // Validation can fail before admission. Sinks can latch that failure even
    // when an application ignores the returned status.
    virtual void onError(NotifyResult) noexcept {}
};

class Notifier {
  public:
    Notifier() noexcept = default;
    Notifier(std::shared_ptr<NotificationSink> sink, ProducerIdentity producer, std::uint64_t subject) noexcept;

    [[nodiscard]] auto active() const noexcept -> bool { return static_cast<bool>(sink_); }
    [[nodiscard]] auto producer() const noexcept -> ProducerIdentity { return producer_; }
    [[nodiscard]] auto subject() const noexcept -> std::uint64_t { return subject_; }
    [[nodiscard]] auto forSubject(std::uint64_t subject) const noexcept -> Notifier;

    [[nodiscard]] auto start(std::string_view label = {}) const noexcept -> NotifyResult;
    [[nodiscard]] auto finish() const noexcept -> NotifyResult;
    [[nodiscard]] auto progress(std::string_view label = {}) const noexcept -> NotifyResult;
    [[nodiscard]] auto notify(std::string_view topic, std::string_view payload) const noexcept -> NotifyResult;
    [[nodiscard]] auto allowSilenceFor(std::chrono::nanoseconds duration, std::string_view reason = {}) const noexcept -> NotifyResult;

  private:
    [[nodiscard]] auto send(NotificationKind kind, std::string_view topic, std::string_view payload,
                            std::chrono::nanoseconds allowance = {}) const noexcept -> NotifyResult;

    std::shared_ptr<NotificationSink> sink_{};
    ProducerIdentity                  producer_{};
    std::uint64_t                     subject_ = 0;
};

enum class WatchdogFailureCode {
    inactivity,
    invalid_configuration,
    capacity_exceeded,
    invalid_record,
    invalid_time,
    resource_exhausted,
    transport_error,
    producer_overload,
    observation_failure,
    process_exited,
    shutdown_timeout,
    closed,
};

struct WatchdogFailure {
    WatchdogFailureCode code = WatchdogFailureCode::inactivity;
    ProducerIdentity    producer{};
    std::uint64_t       subject  = 0;
    std::uint64_t       sequence = 0;
    std::string         label{};
    std::string         detail{};
};

class NotificationObserver {
  public:
    virtual ~NotificationObserver()                               = default;
    virtual void onNotification(const Notification &notification) = 0;
    virtual void onExpired(const WatchdogFailure &failure)        = 0;
};

struct WatchdogConfig {
    std::chrono::nanoseconds inactivityTimeout{};
    std::chrono::nanoseconds maximumSilence  = std::chrono::minutes(5);
    std::size_t              maximumSubjects = 256;
};

enum class WatchdogResult { accepted, ignored, failed };

// The model has one serialized owner and never reads a clock itself. Start arms
// a subject, progress renews only that subject, and finish disarms it. Ordinary
// custom records cannot renew a deadline. An allowance sets a temporary deadline
// to receipt time plus its bounded duration; progress restores the normal timeout.
// Deadline equality expires before any record at that instant is considered.
class WatchdogModel {
  public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit WatchdogModel(WatchdogConfig config) noexcept;
    ~WatchdogModel();
    WatchdogModel(WatchdogModel &&) noexcept;
    auto operator=(WatchdogModel &&) noexcept -> WatchdogModel &;
    WatchdogModel(const WatchdogModel &)                     = delete;
    auto operator=(const WatchdogModel &) -> WatchdogModel & = delete;

    [[nodiscard]] auto accept(const Notification &notification, TimePoint now) noexcept -> WatchdogResult;
    [[nodiscard]] auto advance(TimePoint now) noexcept -> WatchdogResult;
    [[nodiscard]] auto fail(WatchdogFailure failure) noexcept -> WatchdogResult;
    [[nodiscard]] auto nextDeadline() const noexcept -> std::optional<TimePoint>;
    [[nodiscard]] auto failure() const noexcept -> const std::optional<WatchdogFailure> &;
    [[nodiscard]] auto activeSubjects() const noexcept -> std::size_t;

  private:
    struct Impl;
    std::unique_ptr<Impl>          impl_;
    std::optional<WatchdogFailure> setupFailure_;
};

} // namespace gentest
