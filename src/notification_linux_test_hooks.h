#pragma once

#include <atomic>

// Internal seams for the notification transport's own regression tests. They
// are not installed and not part of the public API.
namespace gentest::detail {

// Runs on the watchdog enforcement thread each time a receive batch has drained
// the socket, before the monitor inspects its process owner. A test uses it to
// place a record and the process exit inside that otherwise tiny window.
using WatchdogReceiveDrainedHook = void (*)() noexcept;
extern std::atomic<WatchdogReceiveDrainedHook> watchdogReceiveDrainedHook;

} // namespace gentest::detail
