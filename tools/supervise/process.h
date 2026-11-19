#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace gentest::supervisor {

struct ChildResult {
    // A nonzero exit or a fatal signal is abnormal. On POSIX status holds the
    // exit code or signal number. On Windows it holds the process exit code.
    bool          abnormal;
    unsigned long status;
    bool          timed_out;
};

// Run one child in the standalone supervisor. The caller owns the new capture
// path. The callback may inspect progress and returns true when a deadline expires.
auto runChild(const std::vector<std::string> &command, const std::filesystem::path &output, const std::function<bool()> &expired,
              std::chrono::milliseconds pollInterval = std::chrono::milliseconds(10)) -> ChildResult;

} // namespace gentest::supervisor
