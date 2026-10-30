#include "gentest/notification.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace gentest {
namespace {

constexpr std::size_t wireHeaderSize = 52;

// Keep wire validation identical at ingress and egress, including irrelevant
// fields that could otherwise create multiple encodings of the same event.
auto validate(const Notification &notification, bool requireSequence) noexcept -> NotifyResult {
    if (notification.producer.id == 0 || notification.producer.generation == 0 || notification.subject == 0 ||
        (requireSequence && notification.sequence == 0)) {
        return NotifyResult::invalid_record;
    }
    if (notification.topic.size() > notificationRecordLimit - wireHeaderSize ||
        notification.payload.size() > notificationRecordLimit - wireHeaderSize - notification.topic.size()) {
        return NotifyResult::record_too_large;
    }

    switch (notification.kind) {
    case NotificationKind::subject_started:
    case NotificationKind::progress:
        if (!notification.topic.empty() || notification.allowance.count() != 0) {
            return NotifyResult::invalid_record;
        }
        break;
    case NotificationKind::custom:
        if (notification.topic.empty() || notification.allowance.count() != 0) {
            return NotifyResult::invalid_record;
        }
        break;
    case NotificationKind::silence_allowance:
        if (!notification.topic.empty() || notification.allowance.count() <= 0 ||
            notification.allowance == std::chrono::nanoseconds::max()) {
            return NotifyResult::invalid_record;
        }
        break;
    case NotificationKind::subject_finished:
        if (!notification.topic.empty() || !notification.payload.empty() || notification.allowance.count() != 0) {
            return NotifyResult::invalid_record;
        }
        break;
    default: return NotifyResult::invalid_record;
    }
    return NotifyResult::accepted;
}

// Each span identifies one complete wire field and bounds shifts to the value type.
template <std::size_t Width> void putUnsigned(std::span<std::byte, Width> field, std::uint64_t value) noexcept {
    static_assert(Width > 0 && Width <= sizeof(value));

    for (std::size_t i = 0; i < Width; ++i) {
        field[i] = static_cast<std::byte>((value >> (i * 8)) & 0xffU);
    }
}

template <std::size_t Width> auto getUnsigned(std::span<const std::byte, Width> field) noexcept -> std::uint64_t {
    static_assert(Width > 0 && Width <= sizeof(std::uint64_t));

    std::uint64_t value = 0;
    for (std::size_t i = 0; i < Width; ++i) {
        value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(field[i])) << (i * 8);
    }
    return value;
}

auto deadlineAfter(WatchdogModel::TimePoint now, std::chrono::nanoseconds interval) noexcept -> std::optional<WatchdogModel::TimePoint> {
    using Duration   = WatchdogModel::Clock::duration;
    const auto delta = std::chrono::ceil<Duration>(interval);
    if (delta <= Duration::zero() || now > WatchdogModel::TimePoint::max() - delta) {
        return std::nullopt;
    }
    return now + delta;
}

} // namespace

auto encodeNotification(const Notification &notification, EncodedNotification &encoded) noexcept -> NotifyResult {
    encoded.size      = 0;
    const auto result = validate(notification, true);
    if (result != NotifyResult::accepted) {
        return result;
    }

    const auto size  = wireHeaderSize + notification.topic.size() + notification.payload.size();
    auto       bytes = std::span(encoded.bytes);
    putUnsigned(bytes.subspan<0, 2>(), notificationProtocolVersion);
    putUnsigned(bytes.subspan<2, 1>(), static_cast<std::uint8_t>(notification.kind));
    putUnsigned(bytes.subspan<3, 1>(), 0);
    putUnsigned(bytes.subspan<4, 4>(), size);
    putUnsigned(bytes.subspan<8, 8>(), notification.producer.id);
    putUnsigned(bytes.subspan<16, 8>(), notification.producer.generation);
    putUnsigned(bytes.subspan<24, 8>(), notification.subject);
    putUnsigned(bytes.subspan<32, 8>(), notification.sequence);
    putUnsigned(bytes.subspan<40, 8>(), static_cast<std::uint64_t>(notification.allowance.count()));
    putUnsigned(bytes.subspan<48, 2>(), notification.topic.size());
    putUnsigned(bytes.subspan<50, 2>(), notification.payload.size());

    std::size_t offset = wireHeaderSize;
    for (char ch : notification.topic) {
        bytes[offset++] = static_cast<std::byte>(static_cast<unsigned char>(ch));
    }
    for (char ch : notification.payload) {
        bytes[offset++] = static_cast<std::byte>(static_cast<unsigned char>(ch));
    }
    encoded.size = size;
    return NotifyResult::accepted;
}

auto decodeNotification(std::span<const std::byte> encoded, Notification &notification) noexcept -> NotifyResult {
    notification = {};
    if (encoded.size() > notificationRecordLimit) {
        return NotifyResult::record_too_large;
    }
    if (encoded.size() < wireHeaderSize) {
        return NotifyResult::invalid_record;
    }
    if (getUnsigned(encoded.subspan<0, 2>()) != notificationProtocolVersion) {
        return NotifyResult::unsupported_version;
    }
    if (getUnsigned(encoded.subspan<3, 1>()) != 0 || getUnsigned(encoded.subspan<4, 4>()) != encoded.size()) {
        return NotifyResult::invalid_record;
    }

    const auto topicSize   = static_cast<std::size_t>(getUnsigned(encoded.subspan<48, 2>()));
    const auto payloadSize = static_cast<std::size_t>(getUnsigned(encoded.subspan<50, 2>()));
    const auto allowance   = getUnsigned(encoded.subspan<40, 8>());
    if (topicSize + payloadSize != encoded.size() - wireHeaderSize ||
        allowance > static_cast<std::uint64_t>(std::chrono::nanoseconds::max().count())) {
        return NotifyResult::invalid_record;
    }

    Notification decoded;
    decoded.producer  = {.id = getUnsigned(encoded.subspan<8, 8>()), .generation = getUnsigned(encoded.subspan<16, 8>())};
    decoded.subject   = getUnsigned(encoded.subspan<24, 8>());
    decoded.sequence  = getUnsigned(encoded.subspan<32, 8>());
    decoded.kind      = static_cast<NotificationKind>(getUnsigned(encoded.subspan<2, 1>()));
    decoded.allowance = std::chrono::nanoseconds(static_cast<std::chrono::nanoseconds::rep>(allowance));
    const auto *text  = reinterpret_cast<const char *>(encoded.data() + wireHeaderSize);
    decoded.topic     = {text, topicSize};
    decoded.payload   = {text + topicSize, payloadSize};
    const auto result = validate(decoded, true);
    if (result == NotifyResult::accepted) {
        notification = decoded;
    }
    return result;
}

Notifier::Notifier(std::shared_ptr<NotificationSink> sink, ProducerIdentity producer, std::uint64_t subject) noexcept
    : sink_(std::move(sink)), producer_(producer), subject_(subject) {}

auto Notifier::forSubject(std::uint64_t subject) const noexcept -> Notifier { return {sink_, producer_, subject}; }

auto Notifier::send(NotificationKind kind, std::string_view topic, std::string_view payload,
                    std::chrono::nanoseconds allowance) const noexcept -> NotifyResult {
    if (!sink_) {
        return NotifyResult::disabled;
    }
    const Notification notification{.producer  = producer_,
                                    .subject   = subject_,
                                    .sequence  = 0,
                                    .kind      = kind,
                                    .allowance = allowance,
                                    .topic     = topic,
                                    .payload   = payload};
    const auto         result = validate(notification, false);
    if (result != NotifyResult::accepted) {
        sink_->onError(result);
        return result;
    }
    return sink_->submit(notification);
}

auto Notifier::start(std::string_view label) const noexcept -> NotifyResult { return send(NotificationKind::subject_started, {}, label); }

auto Notifier::finish() const noexcept -> NotifyResult { return send(NotificationKind::subject_finished, {}, {}); }

auto Notifier::progress(std::string_view label) const noexcept -> NotifyResult { return send(NotificationKind::progress, {}, label); }

auto Notifier::notify(std::string_view topic, std::string_view payload) const noexcept -> NotifyResult {
    return send(NotificationKind::custom, topic, payload);
}

auto Notifier::allowSilenceFor(std::chrono::nanoseconds duration, std::string_view reason) const noexcept -> NotifyResult {
    return send(NotificationKind::silence_allowance, {}, reason, duration);
}

struct WatchdogModel::Impl {
    struct Producer {
        ProducerIdentity identity{};
        std::uint64_t    sequence = 0;
    };

    struct Subject {
        ProducerIdentity producer{};
        std::uint64_t    id       = 0;
        std::uint64_t    sequence = 0;
        TimePoint        deadline{};
        std::string      label{};
    };

    explicit Impl(WatchdogConfig configured) : config(configured) {
        subjects.reserve(config.maximumSubjects);
        producers.reserve(config.maximumSubjects);
    }

    WatchdogConfig                 config;
    std::vector<Producer>          producers;
    std::vector<Subject>           subjects;
    std::optional<TimePoint>       previousTime;
    std::optional<WatchdogFailure> failure;
};

WatchdogModel::WatchdogModel(WatchdogConfig config) noexcept {
    if (config.inactivityTimeout <= std::chrono::nanoseconds::zero() || config.inactivityTimeout == std::chrono::nanoseconds::max() ||
        config.maximumSilence <= std::chrono::nanoseconds::zero() || config.maximumSilence == std::chrono::nanoseconds::max() ||
        config.maximumSubjects == 0) {
        setupFailure_.emplace();
        setupFailure_->code = WatchdogFailureCode::invalid_configuration;
        return;
    }

    try {
        impl_ = std::make_unique<Impl>(config);
    } catch (...) {
        setupFailure_.emplace();
        setupFailure_->code = WatchdogFailureCode::resource_exhausted;
    }
}

WatchdogModel::~WatchdogModel()                                             = default;
WatchdogModel::WatchdogModel(WatchdogModel &&) noexcept                     = default;
auto WatchdogModel::operator=(WatchdogModel &&) noexcept -> WatchdogModel & = default;

auto WatchdogModel::fail(WatchdogFailure failure) noexcept -> WatchdogResult {
    if (impl_) {
        if (!impl_->failure) {
            impl_->failure = std::move(failure);
        }
    } else if (!setupFailure_) {
        setupFailure_ = std::move(failure);
    }
    return WatchdogResult::failed;
}

auto WatchdogModel::advance(TimePoint now) noexcept -> WatchdogResult {
    if (!impl_ || impl_->failure) {
        return WatchdogResult::failed;
    }
    if (impl_->previousTime && now < *impl_->previousTime) {
        return fail({.code = WatchdogFailureCode::invalid_time});
    }
    impl_->previousTime = now;

    // Inspect every active subject before consuming a record, so one subject's
    // progress cannot hide another subject's already expired deadline.
    const auto first = std::min_element(impl_->subjects.begin(), impl_->subjects.end(),
                                        [](const auto &left, const auto &right) { return left.deadline < right.deadline; });
    if (first != impl_->subjects.end() && first->deadline <= now) {
        WatchdogFailure failure;
        failure.code     = WatchdogFailureCode::inactivity;
        failure.producer = first->producer;
        failure.subject  = first->id;
        failure.sequence = first->sequence;
        failure.label    = std::move(first->label);
        try {
            failure.detail = "no observed progress before inactivity deadline";
        } catch (...) { failure.code = WatchdogFailureCode::resource_exhausted; }
        return fail(std::move(failure));
    }
    return WatchdogResult::accepted;
}

auto WatchdogModel::accept(const Notification &notification, TimePoint now) noexcept -> WatchdogResult {
    if (advance(now) == WatchdogResult::failed) {
        return WatchdogResult::failed;
    }
    const auto invalid = [&] {
        return fail({.code     = WatchdogFailureCode::invalid_record,
                     .producer = notification.producer,
                     .subject  = notification.subject,
                     .sequence = notification.sequence});
    };
    if (validate(notification, true) != NotifyResult::accepted) {
        return invalid();
    }

    // High-water marks survive finish. Delayed duplicates cannot resurrect a
    // finished subject or renew a new incarnation with the same producer ID.
    auto producer = std::find_if(impl_->producers.begin(), impl_->producers.end(),
                                 [&](const auto &entry) { return entry.identity.id == notification.producer.id; });
    if (producer != impl_->producers.end()) {
        if (notification.producer.generation < producer->identity.generation ||
            (notification.producer == producer->identity && notification.sequence <= producer->sequence)) {
            return WatchdogResult::ignored;
        }
        if (notification.producer.generation > producer->identity.generation && notification.kind != NotificationKind::subject_started) {
            return WatchdogResult::ignored;
        }
    } else if (notification.kind != NotificationKind::subject_started) {
        return WatchdogResult::ignored;
    }

    auto subject = std::find_if(impl_->subjects.begin(), impl_->subjects.end(), [&](const auto &entry) {
        return entry.producer == notification.producer && entry.id == notification.subject;
    });
    if (notification.kind == NotificationKind::subject_started && subject != impl_->subjects.end()) {
        return invalid();
    }
    if (notification.kind == NotificationKind::silence_allowance && notification.allowance > impl_->config.maximumSilence) {
        return invalid();
    }

    try {
        if (producer == impl_->producers.end()) {
            // Producer high-water marks are bounded too. A monitor is intended
            // for one bounded producer session, not an unbounded registry.
            if (impl_->producers.size() == impl_->config.maximumSubjects) {
                return fail({.code     = WatchdogFailureCode::capacity_exceeded,
                             .producer = notification.producer,
                             .subject  = notification.subject,
                             .sequence = notification.sequence});
            }
            impl_->producers.push_back({.identity = notification.producer, .sequence = notification.sequence});
        } else {
            producer->identity = notification.producer;
            producer->sequence = notification.sequence;
        }

        if (notification.kind == NotificationKind::subject_started) {
            if (impl_->subjects.size() == impl_->config.maximumSubjects) {
                return fail({.code     = WatchdogFailureCode::capacity_exceeded,
                             .producer = notification.producer,
                             .subject  = notification.subject,
                             .sequence = notification.sequence});
            }
            const auto deadline = deadlineAfter(now, impl_->config.inactivityTimeout);
            if (!deadline) {
                return fail({.code     = WatchdogFailureCode::invalid_time,
                             .producer = notification.producer,
                             .subject  = notification.subject,
                             .sequence = notification.sequence});
            }
            impl_->subjects.push_back({.producer = notification.producer,
                                       .id       = notification.subject,
                                       .sequence = notification.sequence,
                                       .deadline = *deadline,
                                       .label    = std::string(notification.payload)});
            return WatchdogResult::accepted;
        }
        if (subject == impl_->subjects.end()) {
            return WatchdogResult::ignored;
        }

        subject->sequence = notification.sequence;
        switch (notification.kind) {
        case NotificationKind::progress:
        case NotificationKind::silence_allowance: {
            const auto interval =
                notification.kind == NotificationKind::progress ? impl_->config.inactivityTimeout : notification.allowance;
            const auto deadline = deadlineAfter(now, interval);
            if (!deadline) {
                return fail({.code     = WatchdogFailureCode::invalid_time,
                             .producer = notification.producer,
                             .subject  = notification.subject,
                             .sequence = notification.sequence});
            }
            subject->deadline = *deadline;
            if (notification.kind == NotificationKind::progress && !notification.payload.empty()) {
                subject->label = notification.payload;
            }
            break;
        }
        case NotificationKind::subject_finished: impl_->subjects.erase(subject); break;
        case NotificationKind::custom: break;
        case NotificationKind::subject_started: return invalid();
        }
        return WatchdogResult::accepted;
    } catch (...) {
        return fail({.code     = WatchdogFailureCode::resource_exhausted,
                     .producer = notification.producer,
                     .subject  = notification.subject,
                     .sequence = notification.sequence});
    }
}

auto WatchdogModel::nextDeadline() const noexcept -> std::optional<TimePoint> {
    if (!impl_ || impl_->failure || impl_->subjects.empty()) {
        return std::nullopt;
    }
    const auto first = std::min_element(impl_->subjects.begin(), impl_->subjects.end(),
                                        [](const auto &left, const auto &right) { return left.deadline < right.deadline; });
    return first->deadline;
}

auto WatchdogModel::failure() const noexcept -> const std::optional<WatchdogFailure> & { return impl_ ? impl_->failure : setupFailure_; }

auto WatchdogModel::activeSubjects() const noexcept -> std::size_t { return impl_ ? impl_->subjects.size() : 0; }

} // namespace gentest
