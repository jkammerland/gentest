#pragma once

#include "gentest/runner.h"
#include "runner_cli.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace gentest::runner {

std::uint64_t iteration_shuffle_seed(std::uint64_t base_seed, std::size_t iteration) noexcept;
std::string   failure_rerun_command(const CliOptions &opt, const gentest::Case &test);
std::string   iteration_replay_command(const CliOptions &opt, std::uint64_t seed);

} // namespace gentest::runner
