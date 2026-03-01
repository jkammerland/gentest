#pragma once

#include "gentest/runner.h"

#include "runner_cli.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace gentest::runner {

enum class SelectionStatus {
    Ok,
    CaseNotFound,
    KindMismatch,
    Ambiguous,
    FilterNoBenchMatch,
    FilterNoJitterMatch,
    ZeroSelected,
    DeathExcludedExact,
    DeathExcludedAll,
};

struct SelectionResult {
    SelectionStatus           status = SelectionStatus::Ok;
    std::vector<std::size_t>  idxs;
    std::vector<std::size_t>  ambiguous_matches;
    std::vector<std::size_t>  test_idxs;
    std::vector<std::size_t>  bench_idxs;
    std::vector<std::size_t>  jitter_idxs;
    bool                      has_selection = false;
    std::size_t               filtered_death = 0;
};

SelectionResult select_cases(std::span<const gentest::Case> cases, const CliOptions &opt);
std::string_view kind_to_string(KindFilter kind);

} // namespace gentest::runner
