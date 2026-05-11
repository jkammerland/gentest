#pragma once

#include "gentest/detail/runtime_base.h"

#include <cstddef>
#include <memory>
#include <ostream>
#include <string_view>

namespace gentest {

struct LogSink {
    virtual ~LogSink() = default;
    // May be called concurrently when adopted worker threads log at the same
    // time. Custom sinks own any synchronization they need inside write().
    virtual void write(std::string_view message) noexcept = 0;
};

// Removal token only. Destroying a LogSinkHandle does not unregister the sink;
// call remove() or remove_log_sink() explicitly. Sink registration/removal must
// not run concurrently with gentest::log() or other sink registry changes.
class LogSinkHandle {
  public:
    LogSinkHandle() noexcept                                  = default;
    LogSinkHandle(const LogSinkHandle &)                      = delete;
    LogSinkHandle           &operator=(const LogSinkHandle &) = delete;
    GENTEST_RUNTIME_API      LogSinkHandle(LogSinkHandle &&other) noexcept;
    GENTEST_RUNTIME_API auto operator=(LogSinkHandle &&other) noexcept -> LogSinkHandle &;

    [[nodiscard]] GENTEST_RUNTIME_API auto active() const noexcept -> bool;
    GENTEST_RUNTIME_API auto               remove() noexcept -> bool;

  private:
    friend GENTEST_RUNTIME_API auto add_log_sink(std::shared_ptr<LogSink> sink) -> LogSinkHandle;
    friend GENTEST_RUNTIME_API auto remove_log_sink(LogSinkHandle &handle) noexcept -> bool;

    explicit LogSinkHandle(std::size_t id) noexcept : id_(id) {}

    std::size_t id_ = 0;
};

[[nodiscard]] GENTEST_RUNTIME_API auto add_log_sink(std::shared_ptr<LogSink> sink) -> LogSinkHandle;
GENTEST_RUNTIME_API auto               remove_log_sink(LogSinkHandle &handle) noexcept -> bool;
GENTEST_RUNTIME_API void               remove_all_log_sinks() noexcept;
GENTEST_RUNTIME_API void               restore_default_log_sink();
[[nodiscard]] GENTEST_RUNTIME_API auto make_ostream_log_sink(std::ostream &out) -> std::shared_ptr<LogSink>;

} // namespace gentest
