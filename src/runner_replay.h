#pragma once

#include "gentest/runner.h"
#include "runner_cli.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::runner {

// Shell whose quoting rules a printed rerun/replay command follows.
enum class ReplayShell {
    Posix,
    PowerShell,
};

#if defined(_WIN32)
inline constexpr ReplayShell kNativeReplayShell = ReplayShell::PowerShell;
#else
inline constexpr ReplayShell kNativeReplayShell = ReplayShell::Posix;
#endif

// Pure helpers, independent of the host platform so both dialects stay testable everywhere.
std::string quote_replay_argument(std::string_view value, ReplayShell shell);
std::string replay_command(std::string_view executable, const std::vector<std::string> &arguments, ReplayShell shell);

std::uint64_t iteration_shuffle_seed(std::uint64_t base_seed, std::size_t iteration) noexcept;
std::string   failure_rerun_command(const CliOptions &opt, const gentest::Case &test);
std::string   iteration_replay_command(const CliOptions &opt, std::uint64_t seed);

} // namespace gentest::runner
