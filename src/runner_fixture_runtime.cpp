#include "runner_fixture_runtime.h"

#include "gentest/detail/fixture_runtime.h"
#include "gentest/runner.h"
#include "runner_context_scope.h"

#include <algorithm>
#include <fmt/format.h>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct SharedFixtureEntry {
    std::string                         fixture_name;
    std::string                         suite;
    gentest::detail::SharedFixtureScope scope;
    std::shared_ptr<void>               instance;
    bool                                initialized  = false;
    bool                                initializing = false;
    bool                                failed       = false;
    std::string                         error;
    std::shared_ptr<void> (*create)(std::string_view suite, std::string &error) = nullptr;
    void (*setup)(void *instance, std::string &error)                           = nullptr;
    void (*teardown)(void *instance, std::string &error)                        = nullptr;
};

struct SharedFixtureRegistry {
    std::vector<SharedFixtureEntry> entries;
    std::vector<std::size_t>        activation_order;
    std::mutex                      mtx;
    bool                            teardown_in_progress = false;
    bool                            registration_error   = false;
    std::vector<std::string>        registration_errors;
};

struct SharedFixtureRunGate {
    std::mutex      mtx;
    bool            active = false;
    std::thread::id owner{};
};

auto shared_fixture_registry() -> SharedFixtureRegistry & {
    static SharedFixtureRegistry reg;
    return reg;
}

auto shared_fixture_run_gate() -> SharedFixtureRunGate & {
    static SharedFixtureRunGate gate;
    return gate;
}

bool begin_shared_fixture_run(std::vector<std::string> &errors, gentest::runner::detail::SharedFixtureRuntimeSession &session) {
    session.gate_rejected = false;
    session.owner_thread  = std::thread::id{};
    if (session.owns_gate) {
        session.gate_rejected = true;
        errors.emplace_back("shared fixture runtime run re-entry from the same thread is not supported");
        return false;
    }

    auto                       &gate = shared_fixture_run_gate();
    std::lock_guard<std::mutex> lk(gate.mtx);
    if (gate.active) {
        session.gate_rejected = true;
        if (gate.owner == std::this_thread::get_id()) {
            errors.emplace_back("shared fixture runtime run re-entry from the same thread is not supported");
        } else {
            errors.emplace_back("shared fixture runtime run is already active in another thread");
        }
        return false;
    }
    gate.active           = true;
    gate.owner            = std::this_thread::get_id();
    session.owns_gate     = true;
    session.owner_thread  = gate.owner;
    session.gate_rejected = false;
    return true;
}

bool end_shared_fixture_run(gentest::runner::detail::SharedFixtureRuntimeSession &session, std::string *error) {
    if (!session.owns_gate)
        return true;

    auto                       &gate = shared_fixture_run_gate();
    std::lock_guard<std::mutex> lk(gate.mtx);
    if (gate.active && (gate.owner != session.owner_thread || gate.owner != std::this_thread::get_id())) {
        if (error) {
            *error = "shared fixture runtime session release attempted from non-owner thread";
        }
        return false;
    }
    if (gate.active) {
        // All fixture/context users have drained; restore logging before another
        // invocation can acquire the run gate.
        session.diagnostic_log_route.reset();
        gate.active = false;
        gate.owner  = std::thread::id{};
    }
    session.owns_gate    = false;
    session.owner_thread = std::thread::id{};
    return true;
}

int shared_fixture_scope_rank(gentest::detail::SharedFixtureScope scope) {
    switch (scope) {
    case gentest::detail::SharedFixtureScope::Suite: return 0;
    case gentest::detail::SharedFixtureScope::Global: return 1;
    }
    return 0;
}

bool shared_fixture_order_less(const SharedFixtureEntry &lhs, const SharedFixtureEntry &rhs) {
    if (lhs.fixture_name != rhs.fixture_name)
        return lhs.fixture_name < rhs.fixture_name;
    const int lhs_scope_rank = shared_fixture_scope_rank(lhs.scope);
    const int rhs_scope_rank = shared_fixture_scope_rank(rhs.scope);
    if (lhs_scope_rank != rhs_scope_rank)
        return lhs_scope_rank < rhs_scope_rank;
    return lhs.suite < rhs.suite;
}

void reset_shared_fixture_run_state(SharedFixtureRegistry &reg) {
    reg.activation_order.clear();
    reg.activation_order.reserve(reg.entries.size());
    for (auto &entry : reg.entries) {
        entry.instance.reset();
        entry.initialized  = false;
        entry.initializing = false;
        entry.failed       = false;
        entry.error.clear();
    }
}

bool shared_fixture_callbacks_match(const SharedFixtureEntry &entry, gentest::detail::SharedFixtureCreateFn create,
                                    gentest::detail::SharedFixturePhaseFn setup, gentest::detail::SharedFixturePhaseFn teardown) {
    return entry.create == create && entry.setup == setup && entry.teardown == teardown;
}

bool suite_scope_matches(std::string_view fixture_suite, std::string_view requested_suite) {
    if (fixture_suite.empty()) {
        return true;
    }
    if (requested_suite == fixture_suite) {
        return true;
    }
    if (requested_suite.size() <= fixture_suite.size() || !requested_suite.starts_with(fixture_suite)) {
        return false;
    }

    const auto remainder = requested_suite.substr(fixture_suite.size());
    if (remainder.empty()) {
        return true;
    }
    if (remainder.front() == '/') {
        return true;
    }
    return remainder.size() >= 2 && remainder[0] == ':' && remainder[1] == ':';
}

std::string resolve_fixture_context_issue(const std::shared_ptr<gentest::detail::TestContextInfo> &ctx, std::string current_error,
                                          bool caught_assertion, bool caught_runtime_skip) {
    std::string first_failure = gentest::detail::first_recorded_failure(ctx);
    if (caught_assertion && !first_failure.empty()) {
        return first_failure;
    }
    if (!current_error.empty()) {
        return current_error;
    }
    if (!first_failure.empty()) {
        return first_failure;
    }
    if (gentest::detail::has_bench_error()) {
        return gentest::detail::take_bench_error();
    }
    if (!ctx) {
        if (caught_runtime_skip) {
            return "skip requested without active runtime skip state";
        }
        return current_error;
    }

    std::lock_guard<std::mutex> lk(ctx->mtx);
    if (ctx->runtime_skip_requested.load(std::memory_order_relaxed)) {
        if (!ctx->runtime_skip_reason.empty()) {
            return ctx->runtime_skip_reason;
        }
        return "skip requested";
    }
    if (caught_runtime_skip) {
        return "skip requested without active runtime skip state";
    }
    return current_error;
}

bool run_fixture_phase(std::string_view label, const std::function<void(std::string &)> &fn, std::string &error_out) {
    error_out.clear();
    gentest::detail::clear_bench_error();
    auto ctx                 = gentest::runner::detail::make_active_test_context(label);
    bool caught_assertion    = false;
    bool caught_runtime_skip = false;
    {
        gentest::runner::detail::CurrentTestScope test_scope(ctx);
        try {
            fn(error_out);
        } catch (const gentest::detail::skip_exception &) { caught_runtime_skip = true; } catch (const gentest::assertion &e) {
            caught_assertion = true;
            error_out        = e.message();
        } catch (const std::exception &e) { error_out = fmt::format("std::exception: {}", e.what()); } catch (...) {
            error_out = "unknown exception";
        }
    }
    error_out = resolve_fixture_context_issue(ctx, std::move(error_out), caught_assertion, caught_runtime_skip);
    if (!error_out.empty()) {
        return false;
    }
    return true;
}

std::string format_fixture_error(std::string_view stage, std::string_view detail) {
    if (detail.empty()) {
        return fmt::format("fixture {} failed", stage);
    }
    if (stage == "allocation" && detail == "returned null") {
        return "fixture allocation returned null";
    }
    if (stage == "allocation" && detail.starts_with("std::exception:")) {
        return fmt::format("fixture construction threw {}", detail);
    }
    if (stage == "allocation" && detail == "unknown exception") {
        return "fixture construction threw unknown exception";
    }
    return fmt::format("fixture {} failed: {}", stage, detail);
}

struct FixtureBenchStateScope {
    gentest::detail::BenchPhaseScope phase{gentest::detail::BenchPhase::None};
    std::string                      previous_error = gentest::detail::take_bench_error();

    ~FixtureBenchStateScope() { gentest::detail::bench_error_storage() = std::move(previous_error); }
};

// Factories and setup may acquire other fixtures. Never hold the registry lock
// across callbacks; mark this entry first to reject cycles and self-reentry.
void initialize_shared_fixture(std::size_t target_idx) {
    // A nested activation owns its fixture diagnostics. Restore the caller's
    // pending measurement/setup error when the dependency phase completes.
    FixtureBenchStateScope bench_scope;

    auto                                  &reg = shared_fixture_registry();
    std::string                            fixture_name;
    std::string                            suite_name;
    bool                                   global_recording = false;
    gentest::detail::SharedFixtureCreateFn create_fn        = nullptr;
    gentest::detail::SharedFixturePhaseFn  setup_fn         = nullptr;
    {
        std::lock_guard<std::mutex> lk(reg.mtx);
        const auto                 &entry = reg.entries[target_idx];
        fixture_name                      = entry.fixture_name;
        suite_name                        = entry.suite;
        global_recording                  = entry.scope == gentest::detail::SharedFixtureScope::Global;
        create_fn                         = entry.create;
        setup_fn                          = entry.setup;
    }

    gentest::detail::FrameworkSubjectScope subject(fixture_name, true);
    gentest::detail::RecordingTargetScope  recording_scope(gentest::detail::make_fixture_recording(suite_name, global_recording));
    std::string                            error;
    std::shared_ptr<void>                  instance;
    if (!create_fn) {
        error = "missing factory";
    } else {
        gentest::detail::clear_bench_error();
        auto ctx                 = gentest::runner::detail::make_active_test_context(fmt::format("{} create", fixture_name));
        bool caught_assertion    = false;
        bool caught_runtime_skip = false;
        {
            gentest::runner::detail::CurrentTestScope test_scope(ctx);
            try {
                instance = create_fn(suite_name, error);
            } catch (const gentest::detail::skip_exception &) { caught_runtime_skip = true; } catch (const gentest::assertion &e) {
                caught_assertion = true;
                // Fallback only. resolve_fixture_context_issue() prefers the recorded source-backed failure text.
                error = e.message();
            } catch (const std::exception &e) { error = fmt::format("std::exception: {}", e.what()); } catch (...) {
                error = "unknown exception";
            }
        }
        error = resolve_fixture_context_issue(ctx, std::move(error), caught_assertion, caught_runtime_skip);
    }

    if (!error.empty()) {
        instance.reset();
    }

    if (!instance) {
        std::string fixture_error = create_fn ? format_fixture_error("allocation", error) : "fixture allocation failed: missing factory";
        {
            std::lock_guard<std::mutex> lk(reg.mtx);
            auto                       &entry = reg.entries[target_idx];
            entry.initializing                = false;
            entry.initialized                 = false;
            entry.failed                      = true;
            entry.error                       = fixture_error;
            entry.instance.reset();
        }
        fmt::print(stderr, "gentest: fixture '{}' {}\n", fixture_name, fixture_error);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(reg.mtx);
        reg.entries[target_idx].instance = instance;
    }

    bool setup_ok = true;
    if (setup_fn) {
        const std::string label = fmt::format("fixture setup {}", fixture_name);
        setup_ok                = run_fixture_phase(label, [&](std::string &err) { setup_fn(instance.get(), err); }, error);
    }

    if (!setup_ok) {
        const std::string fixture_error = format_fixture_error("setup", error);
        {
            std::lock_guard<std::mutex> lk(reg.mtx);
            auto                       &entry = reg.entries[target_idx];
            entry.initializing                = false;
            entry.initialized                 = false;
            entry.failed                      = true;
            entry.error                       = fixture_error;
            reg.activation_order.push_back(target_idx);
        }
        fmt::print(stderr, "gentest: fixture '{}' {}\n", fixture_name, fixture_error);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(reg.mtx);
        auto                       &entry = reg.entries[target_idx];
        entry.initializing                = false;
        entry.initialized                 = true;
        entry.failed                      = false;
        entry.error.clear();
        reg.activation_order.push_back(target_idx);
    }
}

} // namespace

namespace gentest::detail {

void register_shared_fixture(SharedFixtureScope scope, std::string_view suite, std::string_view fixture_name, SharedFixtureCreateFn create,
                             SharedFixturePhaseFn setup, SharedFixturePhaseFn teardown) {
    auto                        &reg  = shared_fixture_registry();
    auto                        &gate = shared_fixture_run_gate();
    std::unique_lock<std::mutex> gate_lk(gate.mtx);
    std::lock_guard<std::mutex>  lk(reg.mtx);
    if (gate.active) {
        const std::string msg = fmt::format("fixture '{}' cannot be registered while a test run is active", fixture_name);
        fmt::print(stderr, "gentest: {}\n", msg);
        reg.registration_error = true;
        reg.registration_errors.push_back(msg);
        return;
    }
    for (const auto &entry : reg.entries) {
        if (entry.fixture_name == fixture_name && entry.suite == suite && entry.scope == scope) {
            if (!shared_fixture_callbacks_match(entry, create, setup, teardown)) {
                const std::string msg = fmt::format("fixture '{}' registered multiple times with conflicting callbacks", fixture_name);
                fmt::print(stderr, "gentest: {}\n", msg);
                reg.registration_error = true;
                reg.registration_errors.push_back(msg);
            }
            return;
        }
        if (entry.fixture_name == fixture_name && entry.scope != scope) {
            const std::string msg = fmt::format("fixture '{}' registered with conflicting scopes.", entry.fixture_name);
            fmt::print(stderr, "gentest: {}\n", msg);
            reg.registration_error = true;
            reg.registration_errors.push_back(msg);
            return;
        }
    }
    SharedFixtureEntry entry;
    entry.fixture_name = std::string(fixture_name);
    entry.suite        = std::string(suite);
    entry.scope        = scope;
    entry.create       = create;
    entry.setup        = setup;
    entry.teardown     = teardown;
    auto it            = std::ranges::lower_bound(reg.entries, entry, shared_fixture_order_less);
    reg.entries.insert(it, std::move(entry));
}

bool setup_shared_fixtures() {
    auto &gate = shared_fixture_run_gate();
    {
        std::lock_guard<std::mutex> lk(gate.mtx);
        if (!gate.active || gate.owner != std::this_thread::get_id()) {
            fmt::print(stderr, "gentest: shared fixture setup requires an active runtime session\n");
            return false;
        }
    }

    // Start a fresh run without constructing fixtures. Acquisitions activate
    // only the selected runnable cases' actual dependency closure.
    auto                       &reg = shared_fixture_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    gentest::detail::clear_bench_error();
    reset_shared_fixture_run_state(reg);
    return !reg.registration_error;
}

bool teardown_shared_fixtures(std::vector<std::string> *errors) {
    auto &gate = shared_fixture_run_gate();
    {
        std::lock_guard<std::mutex> lk(gate.mtx);
        if (!gate.active || gate.owner != std::this_thread::get_id()) {
            fmt::print(stderr, "gentest: shared fixture teardown requires an active runtime session\n");
            if (errors) {
                errors->emplace_back("shared fixture teardown requires an active runtime session");
            }
            return false;
        }
    }

    struct TeardownWorkItem {
        std::size_t           index = std::numeric_limits<std::size_t>::max();
        std::string           fixture_name;
        std::string           suite;
        bool                  global_recording = false;
        std::shared_ptr<void> instance;
        void (*teardown)(void *instance, std::string &error) = nullptr;
    };
    struct TeardownGuard {
        SharedFixtureRegistry &reg;
        explicit TeardownGuard(SharedFixtureRegistry &registry) : reg(registry) {
            std::lock_guard<std::mutex> lk(reg.mtx);
            reg.teardown_in_progress = true;
        }
        ~TeardownGuard() {
            std::lock_guard<std::mutex> lk(reg.mtx);
            reg.teardown_in_progress = false;
        }
    };

    auto                         &reg = shared_fixture_registry();
    TeardownGuard                 teardown_guard(reg);
    std::vector<TeardownWorkItem> work;
    {
        std::lock_guard<std::mutex> lk(reg.mtx);
        work.reserve(reg.entries.size());
        for (auto it = reg.activation_order.rbegin(); it != reg.activation_order.rend(); ++it) {
            const auto i     = *it;
            auto      &entry = reg.entries[i];
            if (!entry.instance || (!entry.initialized && !entry.failed)) {
                entry.instance.reset();
                entry.initialized = false;
                continue;
            }
            work.push_back(TeardownWorkItem{
                .index            = i,
                .fixture_name     = entry.fixture_name,
                .suite            = entry.suite,
                .global_recording = entry.scope == gentest::detail::SharedFixtureScope::Global,
                .instance         = entry.instance,
                .teardown         = entry.teardown,
            });
        }
    }

    bool teardown_ok = true;
    for (auto &item : work) {
        gentest::detail::FrameworkSubjectScope subject(item.fixture_name, true);
        gentest::detail::RecordingTargetScope  recording_scope(gentest::detail::make_fixture_recording(item.suite, item.global_recording));
        if (item.teardown) {
            std::string       error;
            const std::string label = fmt::format("fixture teardown {}", item.fixture_name);
            if (!run_fixture_phase(label, [&](std::string &err) { item.teardown(item.instance.get(), err); }, error)) {
                const std::string message = fmt::format("fixture teardown failed for {}: {}", item.fixture_name, error);
                fmt::print(stderr, "gentest: {}\n", message);
                if (errors)
                    errors->push_back(message);
                teardown_ok = false;
            }
        }

        {
            std::lock_guard<std::mutex> lk(reg.mtx);
            auto                       &entry = reg.entries[item.index];
            entry.instance.reset();
            entry.initialized  = false;
            entry.initializing = false;
        }
        // Release the dependent before tearing down its dependencies. A custom
        // deleter may itself query the registry, so run it outside the mutex.
        item.instance.reset();
    }
    return teardown_ok;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::shared_ptr<void> get_shared_fixture(SharedFixtureScope scope, std::string_view suite, std::string_view fixture_name,
                                         std::string &error) {
    error.clear();
    auto &gate         = shared_fixture_run_gate();
    bool  owns_session = false;
    {
        std::lock_guard<std::mutex> lk(gate.mtx);
        owns_session = gate.active && gate.owner == std::this_thread::get_id();
    }

    auto                        &reg = shared_fixture_registry();
    std::unique_lock<std::mutex> lk(reg.mtx);
    if (reg.registration_error) {
        error = reg.registration_errors.empty() ? "fixture registration failed" : reg.registration_errors.front();
        return {};
    }

    std::size_t selected = reg.entries.size();
    for (std::size_t i = 0; i < reg.entries.size(); ++i) {
        const auto &entry = reg.entries[i];
        if (entry.scope != scope || entry.fixture_name != fixture_name || !suite_scope_matches(entry.suite, suite))
            continue;
        if (selected == reg.entries.size() || entry.suite.size() > reg.entries[selected].suite.size())
            selected = i;
    }
    if (selected == reg.entries.size()) {
        error = reg.teardown_in_progress ? "fixture teardown in progress" : "fixture not registered";
        return {};
    }

    // Active sessions reject registration, keeping this entry stable across
    // unlocked recursive acquisitions by factories and setup callbacks.
    auto &entry = reg.entries[selected];
    if (!entry.initialized && !entry.initializing && !entry.failed && !reg.teardown_in_progress) {
        if (!owns_session) {
            error = "fixture initialization requires an active runtime session on its owner thread";
            return {};
        }
        if (gentest::detail::bench_phase() == gentest::detail::BenchPhase::Call) {
            error = "fixture initialization is not allowed during a measured call; acquire it during setup";
            return {};
        }
        entry.initializing = true;
        lk.unlock();
        initialize_shared_fixture(selected);
        lk.lock();
    }

    if (entry.failed) {
        error = entry.error;
        return {};
    }
    if (entry.initializing) {
        error = "fixture initialization in progress";
        return {};
    }
    if (!entry.initialized) {
        error = reg.teardown_in_progress ? "fixture teardown in progress" : "fixture not initialized";
        return {};
    }
    if (!entry.instance) {
        error = "fixture allocation returned null";
        return {};
    }
    return entry.instance;
}

} // namespace gentest::detail

namespace gentest::runner {

bool acquire_case_fixture(const gentest::Case &c, void *&ctx, std::string &reason) {
    ctx = nullptr;
    if (c.fixture_lifetime == FixtureLifetime::None || c.fixture_lifetime == FixtureLifetime::MemberEphemeral)
        return true;
    if (c.fixture.empty()) {
        reason = "fixture allocation returned null";
        return false;
    }
    const auto scope  = (c.fixture_lifetime == FixtureLifetime::MemberSuite) ? gentest::detail::SharedFixtureScope::Suite
                                                                             : gentest::detail::SharedFixtureScope::Global;
    auto       shared = gentest::detail::get_shared_fixture(scope, c.suite, c.fixture, reason);
    if (!shared) {
        if (reason.empty()) {
            reason = "fixture allocation returned null";
        }
        return false;
    }
    ctx = shared.get();
    return true;
}

} // namespace gentest::runner

namespace gentest::runner::detail {

bool setup_shared_fixture_runtime(std::vector<std::string> &errors, SharedFixtureRuntimeSession &session, bool machine_report) {
    errors.clear();
    if (!begin_shared_fixture_run(errors, session)) {
        return false;
    }

    // Only the accepted run may change the default writer. Rejected nested or
    // concurrent runs must not disturb its route, and custom sinks stay intact.
    if (machine_report) {
        session.diagnostic_log_route.emplace(
            stderr, +[](void *stream, std::string_view message) noexcept {
                try {
                    fmt::print(static_cast<FILE *>(stream), "{}\n", message);
                } catch (...) {
                    // Logging is best-effort, matching the default sink.
                    return;
                }
            });
    }

    bool setup_ok = false;
    try {
        setup_ok = gentest::detail::setup_shared_fixtures();
    } catch (const std::exception &e) {
        errors.emplace_back(fmt::format("shared fixture setup threw std::exception: {}", e.what()));
        (void)end_shared_fixture_run(session, nullptr);
        return false;
    } catch (...) {
        errors.emplace_back("shared fixture setup threw unknown exception");
        (void)end_shared_fixture_run(session, nullptr);
        return false;
    }
    if (setup_ok) {
        return true;
    }

    collect_shared_fixture_setup_errors(errors, true);
    if (errors.empty())
        errors.emplace_back("shared fixture setup failed");
    return false;
}

void collect_shared_fixture_setup_errors(std::vector<std::string> &errors, bool include_registration_errors) {
    auto                       &reg = shared_fixture_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    errors.reserve(reg.registration_errors.size() + reg.entries.size());

    // Registration is checked at run entry. A rejected registration attempted
    // during a case retains its existing immediate diagnostic/lookup behavior.
    if (include_registration_errors) {
        for (const auto &msg : reg.registration_errors) {
            if (std::ranges::find(errors, msg) == errors.end())
                errors.push_back(msg);
        }
    }
    for (const auto &entry : reg.entries) {
        if (!entry.failed || entry.error.empty())
            continue;
        const std::string msg = fmt::format("fixture '{}' {}", entry.fixture_name, entry.error);
        if (std::ranges::find(errors, msg) == errors.end()) {
            errors.push_back(msg);
        }
    }
    if (errors.empty() && include_registration_errors && reg.registration_error) {
        errors.emplace_back("shared fixture registration failed");
    }
}

bool teardown_shared_fixture_runtime(std::vector<std::string> &errors, SharedFixtureRuntimeSession &session) {
    errors.clear();
    if (!session.owns_gate) {
        return true;
    }

    bool teardown_ok = false;
    try {
        teardown_ok = gentest::detail::teardown_shared_fixtures(&errors);
    } catch (const std::exception &e) {
        errors.emplace_back(fmt::format("shared fixture teardown threw std::exception: {}", e.what()));
        teardown_ok = false;
    } catch (...) {
        errors.emplace_back("shared fixture teardown threw unknown exception");
        teardown_ok = false;
    }
    std::string release_error;
    if (!end_shared_fixture_run(session, &release_error)) {
        errors.push_back(release_error);
        teardown_ok = false;
    }
    return teardown_ok;
}

} // namespace gentest::runner::detail
