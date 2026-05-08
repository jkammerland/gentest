#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fixture::third_party {

template <typename T> struct Identity {
    using type = T;
};

template <typename T> using Alias = typename Identity<T>::type;

namespace aliases {

using Text      = Alias<std::string>;
using OwnedInt  = Alias<std::unique_ptr<int>>;
using IntPair   = Alias<std::pair<int, int>>;
using PairBatch = Alias<std::vector<IntPair>>;

} // namespace aliases

struct Calculator {
    virtual ~Calculator()                     = default;
    virtual int         add(int lhs, int rhs) = 0;
    virtual std::string name() const noexcept = 0;
};

struct ResourceFactory {
    virtual ~ResourceFactory()                                   = default;
    virtual aliases::OwnedInt make(aliases::Text label)          = 0;
    virtual int              &value()                            = 0;
    virtual aliases::IntPair  combine(aliases::PairBatch values) = 0;
    virtual bool              consume(aliases::OwnedInt ptr)     = 0;
};

} // namespace fixture::third_party
