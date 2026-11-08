#pragma once

// Regression coverage for generated mock signatures that name types from user
// top-level namespaces which share a name with a gentest:: sub-namespace.
// Generated mock specializations live inside namespace gentest, so every nested
// name in a generated signature must be globally qualified; otherwise lookup of
// e.g. `detail::Handle` finds gentest::detail first and fails.

#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace detail {
struct Handle {
    int         id                                         = 0;
    friend bool operator==(const Handle &, const Handle &) = default;
};
enum class Kind { first, second };
} // namespace detail

namespace async {
struct Job {
    int         id                                   = 0;
    friend bool operator==(const Job &, const Job &) = default;
};
} // namespace async

namespace asserts {
struct Check {
    int         id                                       = 0;
    friend bool operator==(const Check &, const Check &) = default;
};
} // namespace asserts

namespace match {
struct Pattern {
    int         id                                           = 0;
    friend bool operator==(const Pattern &, const Pattern &) = default;
};
} // namespace match

namespace approx {
struct Tolerance {
    double      value                                            = 0.0;
    friend bool operator==(const Tolerance &, const Tolerance &) = default;
};
} // namespace approx

namespace mocking {

struct CollidingNamespaces {
    virtual ~CollidingNamespaces() = default;

    // Parameter and return positions.
    virtual void       take(detail::Handle handle) = 0;
    virtual async::Job next()                      = 0;

    // Nested template arguments, including function types inside template arguments
    // (function pointers rather than std::function: mocked arguments must be
    // equality comparable).
    virtual std::shared_ptr<detail::Handle>              share(const std::vector<async::Job> &jobs)                           = 0;
    virtual std::map<int, std::optional<match::Pattern>> table(const std::pair<approx::Tolerance, detail::Kind> &key)         = 0;
    virtual void                              on_done(std::optional<void (*)(const async::Job &, detail::Handle *)> callback) = 0;
    virtual asserts::Check                   *find(const std::vector<asserts::Check *(*)(match::Pattern)> &pickers)           = 0;
    virtual std::unique_ptr<detail::Handle[]> handles(std::vector<int detail::Handle::*> members)                             = 0;

    // Member template: written template parameter names must stay unqualified.
    template <typename T> std::vector<detail::Handle> collect(T value, const std::vector<async::Job> &jobs) {
        (void)value;
        (void)jobs;
        return {};
    }
    // Non-type template parameter type and default template argument type.
    template <detail::Kind K> int               tagged(detail::Handle handle) { return handle.id + static_cast<int>(K); }
    template <typename U = detail::Handle> void defaulted(U value) { (void)value; }
};

// Class template specialization whose template argument lives in a colliding namespace.
template <typename T> struct Store {
    virtual ~Store()                        = default;
    virtual T    get() const                = 0;
    virtual void put(std::vector<T> values) = 0;
};

} // namespace mocking
