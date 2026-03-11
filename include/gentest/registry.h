#pragma once

#include "gentest/detail/runtime_base.h"
#include "gentest/fixture.h"

#include <exception>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace gentest {

// Unified test entry (argc/argv version). Consumed by generated code.
GENTEST_RUNTIME_API auto run_all_tests(int argc, char **argv) -> int;
// Unified test entry (span version). Consumed by generated code.
GENTEST_RUNTIME_API auto run_all_tests(std::span<const char *> args) -> int;

// Runtime-visible test case description used by the generated manifest.
// The generator produces a constexpr array of Case entries and provides access
// via gentest::get_cases()/gentest::get_case_count() defined in the generated TU.
enum class FixtureLifetime {
    None,
    MemberEphemeral,
    MemberSuite,
    MemberGlobal,
};

struct Case {
    std::string_view name;
    void (*fn)(void *);
    std::string_view                  file;
    unsigned                          line;
    bool                              is_benchmark{false};
    bool                              is_jitter{false};
    bool                              is_baseline{false};
    std::span<const std::string_view> tags;
    std::span<const std::string_view> requirements;
    std::string_view                  skip_reason;
    bool                              should_skip;
    std::string_view                  fixture; // empty for free tests
    FixtureLifetime                   fixture_lifetime;
    std::string_view                  suite;
};

// Provided by the runtime registry; populated by generated translation units.
GENTEST_RUNTIME_API const Case *get_cases();
GENTEST_RUNTIME_API std::size_t get_case_count();

namespace detail {

inline constexpr bool exceptions_enabled = GENTEST_EXCEPTIONS_ENABLED != 0;

// Called by generated sources to register discovered cases. Not intended for
// direct use in test code.
GENTEST_RUNTIME_API void register_cases(std::span<const Case> cases);

// Returns an owned, sorted snapshot of the currently registered cases. Used by
// the runtime to keep an active run insulated from later registrations.
GENTEST_RUNTIME_API auto snapshot_registered_cases() -> std::vector<Case>;

enum class SharedFixtureScope {
    Suite,
    Global,
};

struct SharedFixtureRegistration {
    std::string_view   fixture_name;
    std::string_view   suite;
    SharedFixtureScope scope;
    std::shared_ptr<void> (*create)(std::string_view suite, std::string &error);
    void (*setup)(void *instance, std::string &error);
    void (*teardown)(void *instance, std::string &error);
};

// Runtime registry for suite/global fixtures. Generated code calls
// register_shared_fixture during static initialization.
GENTEST_RUNTIME_API void register_shared_fixture(const SharedFixtureRegistration &registration);

// Setup/teardown shared fixtures before/after the test run. setup returns false
// when shared fixture infrastructure fails (for example conflicting
// registrations, allocation failures, or setup failures).
GENTEST_RUNTIME_API bool setup_shared_fixtures();
GENTEST_RUNTIME_API bool teardown_shared_fixtures(std::vector<std::string> *errors = nullptr);

// Lookup shared fixture instance by scope/suite/name. Returns nullptr and fills
// error when unavailable (not registered, allocation/setup failure, or setup
// currently in progress due to reentrant lookup).
GENTEST_RUNTIME_API std::shared_ptr<void> get_shared_fixture(SharedFixtureScope scope, std::string_view suite,
                                                             std::string_view fixture_name, std::string &error);

namespace detail_internal {

template <typename Fixture> inline std::shared_ptr<void> shared_fixture_create(std::string_view suite, std::string &error) {
#if GENTEST_EXCEPTIONS_ENABLED
    try {
        auto handle = FixtureHandle<Fixture>::empty();
        if (!handle.init(suite)) {
            error = "returned null";
            return {};
        }
        return handle.shared();
    } catch (const std::exception &e) {
        error = std::string("std::exception: ") + e.what();
        return {};
    } catch (...) {
        error = "unknown exception";
        return {};
    }
#else
    auto handle = FixtureHandle<Fixture>::empty();
    if (!handle.init(suite)) {
        error = "returned null";
        return {};
    }
    return handle.shared();
#endif
}

template <typename Fixture> inline void shared_fixture_setup(void *instance, std::string &error) {
    if constexpr (std::is_base_of_v<gentest::FixtureSetup, Fixture>) {
        if (!instance) {
            error = "instance missing";
            return;
        }
#if GENTEST_EXCEPTIONS_ENABLED
        try {
            static_cast<Fixture *>(instance)->setUp();
        } catch (const gentest::assertion &e) { error = e.message(); } catch (const std::exception &e) {
            error = std::string("std::exception: ") + e.what();
        } catch (...) { error = "unknown exception"; }
#else
        static_cast<Fixture *>(instance)->setUp();
#endif
    }
}

template <typename Fixture> inline void shared_fixture_teardown(void *instance, std::string &error) {
    if constexpr (std::is_base_of_v<gentest::FixtureTearDown, Fixture>) {
        if (!instance) {
            error = "instance missing";
            return;
        }
#if GENTEST_EXCEPTIONS_ENABLED
        try {
            static_cast<Fixture *>(instance)->tearDown();
        } catch (const gentest::assertion &e) { error = e.message(); } catch (const std::exception &e) {
            error = std::string("std::exception: ") + e.what();
        } catch (...) { error = "unknown exception"; }
#else
        static_cast<Fixture *>(instance)->tearDown();
#endif
    }
}

} // namespace detail_internal

template <typename Fixture>
inline void register_shared_fixture(SharedFixtureScope scope, std::string_view suite, std::string_view fixture_name) {
    SharedFixtureRegistration reg{
        .fixture_name = fixture_name,
        .suite        = suite,
        .scope        = scope,
        .create       = &detail_internal::shared_fixture_create<Fixture>,
        .setup        = &detail_internal::shared_fixture_setup<Fixture>,
        .teardown     = &detail_internal::shared_fixture_teardown<Fixture>,
    };
    register_shared_fixture(reg);
}

template <typename Fixture>
inline std::shared_ptr<Fixture> get_shared_fixture_typed(SharedFixtureScope scope, std::string_view suite, std::string_view fixture_name,
                                                         std::string &error) {
    auto raw = get_shared_fixture(scope, suite, fixture_name, error);
    return std::static_pointer_cast<Fixture>(raw);
}

} // namespace detail

} // namespace gentest
