#pragma once

#include "gentest/notification.h"

namespace gentest::detail {

// A sink that wraps another notifier must forward pre-admission errors too.
// The supervised process can terminate before the runner returns its status.
struct NotificationForwarder {
    static void onError(const Notifier &target, NotifyResult result) noexcept {
        if (target.sink_)
            target.sink_->onError(result);
    }
    static auto submit(const Notifier &target, const Notification &record) noexcept -> NotifyResult {
        return target.send(record.kind, record.topic, record.payload, record.allowance);
    }
};
} // namespace gentest::detail
