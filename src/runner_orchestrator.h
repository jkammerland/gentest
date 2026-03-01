#pragma once

#include "gentest/runner.h"
#include "runner_cli.h"

#include <span>

namespace gentest::runner {

int run_from_options(std::span<const gentest::Case> cases, const CliOptions &opt);

} // namespace gentest::runner
