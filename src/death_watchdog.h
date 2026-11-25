#pragma once

#include "gentest/notification.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace gentest::detail {

// One invocation owns the file and the receiving end of the notification channel.
class DeathWatchdog {
  public:
    DeathWatchdog(const std::filesystem::path &path, std::chrono::milliseconds inactivity);
    ~DeathWatchdog();
    void               check();
    void               finish();
    [[nodiscard]] bool attached() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

#if defined(_WIN32)
auto wideProcessArguments(int first = 1) -> std::vector<std::string>;
#endif

// Keep a caller-supplied notifier active while forwarding the same events to the supervisor.
Notifier deathWatchdogNotifier(const std::filesystem::path &path, const Notifier &upstream);
} // namespace gentest::detail
