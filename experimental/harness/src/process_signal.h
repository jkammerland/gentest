#pragma once

#include "common.h"

#include <csignal>
#include <sys/syscall.h>

namespace gentest::harness::detail {
inline void validateSignal(int signal) {
    if (signal < 0 || signal >= NSIG) {
        throw std::invalid_argument("invalid process signal");
    }
}

// The retained pidfd identifies this process even after its PID is recycled.
// False includes exit between the caller's collection and kernel submission.
inline auto submitSignal(int pidfd, bool exited, int signal) -> bool {
    validateSignal(signal);
    if (exited) {
        return false;
    }
    if (::syscall(SYS_pidfd_send_signal, pidfd, signal, nullptr, 0) == 0) {
        return true;
    }
    if (errno == ESRCH) {
        return false;
    }
    throw std::system_error(errno, std::generic_category(), "pidfd_send_signal");
}
} // namespace gentest::harness::detail
