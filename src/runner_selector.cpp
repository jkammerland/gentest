#include "runner_selector.h"

#include <algorithm>

namespace gentest::runner {

namespace {

bool case_is_test(const gentest::Case &c) { return !c.is_benchmark && !c.is_jitter; }

bool case_matches_kind(const gentest::Case &c, KindFilter kind) {
    switch (kind) {
    case KindFilter::All: return true;
    case KindFilter::Test: return case_is_test(c);
    case KindFilter::Bench: return c.is_benchmark;
    case KindFilter::Jitter: return c.is_jitter;
    }
    return true;
}

bool wildcard_match(std::string_view text, std::string_view pattern) {
    std::size_t ti = 0, pi = 0, star = std::string_view::npos, mark = 0;
    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti])) {
            ++ti;
            ++pi;
            continue;
        }
        if (pi < pattern.size() && pattern[pi] == '*') {
            star = pi++;
            mark = ti;
            continue;
        }
        if (star != std::string_view::npos) {
            pi = star + 1;
            ti = ++mark;
            continue;
        }
        return false;
    }
    while (pi < pattern.size() && pattern[pi] == '*')
        ++pi;
    return pi == pattern.size();
}

bool iequals(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const char a = lhs[i];
        const char b = rhs[i];
        if (a == b)
            continue;
        const char al = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
        const char bl = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
        if (al != bl)
            return false;
    }
    return true;
}

bool has_tag_ci(const gentest::Case &test, std::string_view tag) {
    for (auto t : test.tags) {
        if (iequals(t, tag))
            return true;
    }
    return false;
}

void split_selected_cases(std::span<const gentest::Case> cases, const std::vector<std::size_t> &idxs, SelectionResult &out) {
    out.test_idxs.reserve(idxs.size());
    out.bench_idxs.reserve(idxs.size());
    out.jitter_idxs.reserve(idxs.size());
    for (auto idx : idxs) {
        if (cases[idx].is_benchmark)
            out.bench_idxs.push_back(idx);
        else if (cases[idx].is_jitter)
            out.jitter_idxs.push_back(idx);
        else
            out.test_idxs.push_back(idx);
    }
}

} // namespace

std::string_view kind_to_string(KindFilter kind) {
    switch (kind) {
    case KindFilter::All: return "all";
    case KindFilter::Test: return "test";
    case KindFilter::Bench: return "bench";
    case KindFilter::Jitter: return "jitter";
    }
    return "all";
}

SelectionResult select_cases(std::span<const gentest::Case> cases, const CliOptions &opt) {
    SelectionResult          result;
    std::vector<std::size_t> idxs;
    result.has_selection = (opt.run_exact != nullptr) || (opt.filter_pat != nullptr);

    if (opt.run_exact) {
        const std::string_view exact = opt.run_exact;

        std::vector<std::size_t> exact_matches;
        std::vector<std::size_t> exact_kind_matches;
        for (std::size_t i = 0; i < cases.size(); ++i) {
            if (cases[i].name != exact)
                continue;
            exact_matches.push_back(i);
            if (case_matches_kind(cases[i], opt.kind))
                exact_kind_matches.push_back(i);
        }

        if (!exact_matches.empty()) {
            if (exact_kind_matches.empty()) {
                result.status = SelectionStatus::KindMismatch;
                return result;
            }
            if (exact_kind_matches.size() > 1) {
                result.status            = SelectionStatus::Ambiguous;
                result.ambiguous_matches = std::move(exact_kind_matches);
                return result;
            }
            idxs.push_back(exact_kind_matches.front());
        } else {
            std::vector<std::size_t> suffix_matches;
            std::vector<std::size_t> suffix_kind_matches;
            for (std::size_t i = 0; i < cases.size(); ++i) {
                if (cases[i].name.size() < exact.size())
                    continue;
                if (!cases[i].name.ends_with(exact))
                    continue;
                suffix_matches.push_back(i);
                if (case_matches_kind(cases[i], opt.kind))
                    suffix_kind_matches.push_back(i);
            }
            if (suffix_matches.empty()) {
                result.status = SelectionStatus::CaseNotFound;
                return result;
            }
            if (suffix_kind_matches.empty()) {
                result.status = SelectionStatus::KindMismatch;
                return result;
            }
            if (suffix_kind_matches.size() > 1) {
                result.status            = SelectionStatus::Ambiguous;
                result.ambiguous_matches = std::move(suffix_kind_matches);
                return result;
            }
            idxs.push_back(suffix_kind_matches.front());
        }
    } else if (opt.filter_pat) {
        for (std::size_t i = 0; i < cases.size(); ++i) {
            if (wildcard_match(cases[i].name, opt.filter_pat))
                idxs.push_back(i);
        }
    } else {
        idxs.resize(cases.size());
        for (std::size_t i = 0; i < cases.size(); ++i)
            idxs[i] = i;
    }

    {
        std::vector<std::size_t> kept;
        kept.reserve(idxs.size());
        for (auto idx : idxs) {
            if (case_matches_kind(cases[idx], opt.kind))
                kept.push_back(idx);
        }
        idxs = std::move(kept);
    }

    if (idxs.empty()) {
        if (opt.filter_pat && opt.kind == KindFilter::Bench) {
            result.status = SelectionStatus::FilterNoBenchMatch;
            return result;
        }
        if (opt.filter_pat && opt.kind == KindFilter::Jitter) {
            result.status = SelectionStatus::FilterNoJitterMatch;
            return result;
        }
        result.status = SelectionStatus::ZeroSelected;
        return result;
    }

    if (!opt.include_death) {
        std::vector<std::size_t> kept;
        kept.reserve(idxs.size());
        for (auto idx : idxs) {
            if (has_tag_ci(cases[idx], "death")) {
                ++result.filtered_death;
                continue;
            }
            kept.push_back(idx);
        }
        if (kept.empty() && result.filtered_death > 0) {
            result.status = opt.run_exact ? SelectionStatus::DeathExcludedExact : SelectionStatus::DeathExcludedAll;
            return result;
        }
        idxs = std::move(kept);
    }

    result.status = SelectionStatus::Ok;
    result.idxs   = idxs;
    split_selected_cases(cases, idxs, result);
    return result;
}

} // namespace gentest::runner
