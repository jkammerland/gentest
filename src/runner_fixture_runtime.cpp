#include "runner_fixture_runtime.h"

#include "gentest/runner.h"

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

bool shared_fixture_callbacks_match(const SharedFixtureEntry &entry, const gentest::detail::SharedFixtureRegistration &registration) {
    return entry.create == registration.create && entry.setup == registration.setup && entry.teardown == registration.teardown;
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

struct FixtureContextGuard {
    std::shared_ptr<gentest::detail::TestContextInfo> ctx;
    explicit FixtureContextGuard(std::string_view name) {
        ctx               = std::make_shared<gentest::detail::TestContextInfo>();
        ctx->display_name = std::string(name);
        ctx->active       = true;
        gentest::detail::set_current_test(ctx);
    }
    ~FixtureContextGuard() {
        if (ctx) {
            ctx->active = false;
            gentest::detail::set_current_test(nullptr);
        }
    }
};

bool run_fixture_phase(std::string_view label, const std::function<void(std::string &)> &fn, std::string &error_out) {
    error_out.clear();
    gentest::detail::clear_bench_error();
    FixtureContextGuard guard(label);
    try {
        fn(error_out);
    } catch (const gentest::assertion &e) { error_out = e.message(); } catch (const std::exception &e) {
        error_out = std::string("std::exception: ") + e.what();
    } catch (...) { error_out = "unknown exception"; }
    gentest::detail::wait_for_adopted_tokens(guard.ctx);
    gentest::detail::flush_current_buffer_for(guard.ctx.get());
    if (!error_out.empty()) {
        return false;
    }
    if (gentest::detail::has_bench_error()) {
        error_out = gentest::detail::take_bench_error();
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(guard.ctx->mtx);
        if (!guard.ctx->failures.empty()) {
            error_out = guard.ctx->failures.front();
            return false;
        }
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

} // namespace

namespace gentest::detail {

void register_shared_fixture(const SharedFixtureRegistration &registration) {
    auto                        &reg  = shared_fixture_registry();
    auto                        &gate = shared_fixture_run_gate();
    std::unique_lock<std::mutex> gate_lk(gate.mtx);
    std::lock_guard<std::mutex>  lk(reg.mtx);
    if (gate.active) {
        const std::string msg = fmt::format("fixture '{}' cannot be registered while a test run is active", registration.fixture_name);
        fmt::print(stderr, "gentest: {}\n", msg);
        reg.registration_error = true;
        reg.registration_errors.push_back(msg);
        return;
    }
    for (const auto &entry : reg.entries) {
        if (entry.fixture_name == registration.fixture_name && entry.suite == registration.suite && entry.scope == registration.scope) {
            if (!shared_fixture_callbacks_match(entry, registration)) {
                const std::string msg =
                    fmt::format("fixture '{}' registered multiple times with conflicting callbacks", registration.fixture_name);
                fmt::print(stderr, "gentest: {}\n", msg);
                reg.registration_error = true;
                reg.registration_errors.push_back(msg);
            }
            return;
        }
        if (entry.fixture_name == registration.fixture_name && entry.scope != registration.scope) {
            const std::string msg = fmt::format("fixture '{}' registered with conflicting scopes.", entry.fixture_name);
            fmt::print(stderr, "gentest: {}\n", msg);
            reg.registration_error = true;
            reg.registration_errors.push_back(msg);
            return;
        }
    }
    SharedFixtureEntry entry;
    entry.fixture_name = std::string(registration.fixture_name);
    entry.suite        = std::string(registration.suite);
    entry.scope        = registration.scope;
    entry.create       = registration.create;
    entry.setup        = registration.setup;
    entry.teardown     = registration.teardown;
    auto it            = std::lower_bound(reg.entries.begin(), reg.entries.end(), entry, shared_fixture_order_less);
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

    auto &reg = shared_fixture_registry();
    bool  ok  = true;
    {
        std::lock_guard<std::mutex> lk(reg.mtx);
        if (reg.registration_error) {
            return false;
        }
    }
    for (;;) {
        std::size_t target_idx = std::numeric_limits<std::size_t>::max();
        std::string fixture_name;
        std::string suite_name;
        bool        teardown_in_progress                                    = false;
        std::shared_ptr<void> (*create_fn)(std::string_view, std::string &) = nullptr;
        void (*setup_fn)(void *, std::string &)                             = nullptr;

        {
            std::lock_guard<std::mutex> lk(reg.mtx);
            teardown_in_progress = reg.teardown_in_progress;
            if (!teardown_in_progress) {
                for (std::size_t i = 0; i < reg.entries.size(); ++i) {
                    auto &entry = reg.entries[i];
                    if (entry.initialized || entry.initializing || entry.failed) {
                        continue;
                    }
                    entry.initializing = true;
                    target_idx         = i;
                    fixture_name       = entry.fixture_name;
                    suite_name         = entry.suite;
                    create_fn          = entry.create;
                    setup_fn           = entry.setup;
                    break;
                }
            }
        }

        if (teardown_in_progress) {
            break;
        }
        if (target_idx == std::numeric_limits<std::size_t>::max()) {
            break;
        }

        std::string           error;
        std::shared_ptr<void> instance;
        if (!create_fn) {
            error = "missing factory";
        } else {
            try {
                instance = create_fn(suite_name, error);
            } catch (const gentest::assertion &e) { error = e.message(); } catch (const std::exception &e) {
                error = std::string("std::exception: ") + e.what();
            } catch (...) { error = "unknown exception"; }
        }

        if (!instance) {
            ok = false;
            std::string fixture_error =
                create_fn ? format_fixture_error("allocation", error) : "fixture allocation failed: missing factory";
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
            continue;
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
            ok                              = false;
            const std::string fixture_error = format_fixture_error("setup", error);
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
            continue;
        }

        {
            std::lock_guard<std::mutex> lk(reg.mtx);
            auto                       &entry = reg.entries[target_idx];
            entry.initializing                = false;
            entry.initialized                 = true;
            entry.failed                      = false;
            entry.error.clear();
        }
    }
    return ok;
}

bool teardown_shared_fixtures(std::vector<std::string> *errors) {
    auto &gate = shared_fixture_run_gate();
    {
        std::lock_guard<std::mutex> lk(gate.mtx);
        if (!gate.active || gate.owner != std::this_thread::get_id()) {
            fmt::print(stderr, "gentest: shared fixture teardown requires an active runtime session\n");
            if (errors) {
                errors->push_back("shared fixture teardown requires an active runtime session");
            }
            return false;
        }
    }

    struct TeardownWorkItem {
        std::size_t           index = std::numeric_limits<std::size_t>::max();
        std::string           fixture_name;
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
        for (std::size_t i = reg.entries.size(); i-- > 0;) {
            auto &entry = reg.entries[i];
            if (!entry.initialized || entry.failed) {
                entry.instance.reset();
                entry.initialized = false;
                continue;
            }
            work.push_back(TeardownWorkItem{
                .index        = i,
                .fixture_name = entry.fixture_name,
                .instance     = entry.instance,
                .teardown     = entry.teardown,
            });
        }
    }

    bool teardown_ok = true;
    for (const auto &item : work) {
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

        std::lock_guard<std::mutex> lk(reg.mtx);
        if (item.index < reg.entries.size()) {
            auto &entry = reg.entries[item.index];
            entry.instance.reset();
            entry.initialized  = false;
            entry.initializing = false;
        }
    }
    return teardown_ok;
}

std::shared_ptr<void> get_shared_fixture(SharedFixtureScope scope, std::string_view suite, std::string_view fixture_name,
                                         std::string &error) {
    auto                       &reg = shared_fixture_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    if (reg.registration_error) {
        if (!reg.registration_errors.empty()) {
            error = reg.registration_errors.front();
        } else {
            error = "fixture registration failed";
        }
        return {};
    }
    SharedFixtureEntry *selected = nullptr;
    for (auto &entry : reg.entries) {
        if (entry.scope != scope)
            continue;
        if (entry.fixture_name != fixture_name)
            continue;
        if (scope == SharedFixtureScope::Suite) {
            if (!suite_scope_matches(entry.suite, suite))
                continue;
            if (!selected || entry.suite.size() > selected->suite.size()) {
                selected = &entry;
            }
            continue;
        }
        selected = &entry;
        break;
    }
    if (!selected) {
        if (reg.teardown_in_progress) {
            error = "fixture teardown in progress";
            return {};
        }
        error = "fixture not registered";
        return {};
    }

    if (selected->failed) {
        error = selected->error;
        return {};
    }
    if (selected->initializing) {
        error = "fixture initialization in progress";
        return {};
    }
    if (!selected->initialized) {
        error = reg.teardown_in_progress ? "fixture teardown in progress" : "fixture not initialized";
        return {};
    }
    if (!selected->instance) {
        error = "fixture allocation returned null";
        return {};
    }
    return selected->instance;
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

bool setup_shared_fixture_runtime(std::vector<std::string> &errors, SharedFixtureRuntimeSession &session) {
    errors.clear();
    if (!begin_shared_fixture_run(errors, session)) {
        return false;
    }

    bool setup_ok = false;
    try {
        setup_ok = gentest::detail::setup_shared_fixtures();
    } catch (const std::exception &e) {
        errors.emplace_back(std::string("shared fixture setup threw std::exception: ") + e.what());
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

    auto                       &reg = shared_fixture_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    errors.reserve(reg.registration_errors.size() + reg.entries.size());

    for (const auto &msg : reg.registration_errors) {
        if (std::find(errors.begin(), errors.end(), msg) == errors.end()) {
            errors.push_back(msg);
        }
    }
    for (const auto &entry : reg.entries) {
        if (!entry.failed || entry.error.empty())
            continue;
        const std::string msg = fmt::format("fixture '{}' {}", entry.fixture_name, entry.error);
        if (std::find(errors.begin(), errors.end(), msg) == errors.end()) {
            errors.push_back(msg);
        }
    }
    if (errors.empty() && reg.registration_error) {
        errors.emplace_back("shared fixture registration failed");
    }
    if (errors.empty()) {
        errors.emplace_back("shared fixture setup failed");
    }
    return false;
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
        errors.emplace_back(std::string("shared fixture teardown threw std::exception: ") + e.what());
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
