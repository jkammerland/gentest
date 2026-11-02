// The mock surface stages this header; one guard covers both include paths.
#ifndef GENTEST_BENCH_CYCLE_PRODUCTION_HPP
#define GENTEST_BENCH_CYCLE_PRODUCTION_HPP

#include <algorithm>

namespace app {

inline int normalizedCents(int value) { return std::clamp(value, 0, 100); }

struct Ledger {
    virtual ~Ledger()             = default;
    virtual bool debit(int cents) = 0;
};

inline bool charge(Ledger &ledger, int cents) { return ledger.debit(normalizedCents(cents)); }

} // namespace app

#endif
