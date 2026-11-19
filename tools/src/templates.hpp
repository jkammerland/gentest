// Embedded templates for code generation.
//
// These string constants define generated registration building blocks. They are consumed by the
// emitter (emit.cpp) and rendered via fmt::format (partials) or plain
// placeholder replacement. Keeping templates in one header makes them easy to
// locate, review, and change.
//
// Placeholders and where they apply:
// - Shared registration fragment (registration_common):
//     {{FORWARD_DECLS}}, {{TRAIT_DECLS}}, {{WRAPPER_IMPLS}}
// - TU registration headers:
//     {{REGISTRATION_PREAMBLE}}, {{WRAPPER_SUPPORT_COMMON}},
//     {{GLOBAL_WRAPPER_IMPLS}}, {{REGISTRATION_COMMON}}, {{CASE_INITS}},
//     {{FIXTURE_REGISTRATIONS}}
// - Partials (formatted with fmt::format):
//   wrapper_free_test:{w}, {invoke}
//   wrapper_free:     {w}, {invoke}
//   wrapper_free_fixtures: {w}, {fn}, {decls}, {setup_flags}, {setup}, {teardown}, {call},
//                          {bench_decls}, {bench_inits}, {bench_setup}, {bench_teardown}, {bench_invoke}
//   wrapper_ephemeral:{w}, {fixture}, {method}, {bench_invoke}
//   wrapper_stateful: {w}, {fixture}, {method}
//   case_entry:       {name}, {wrapper}, {file}, {line}, {tags}, {reqs},
//                     {skip_reason}, {should_skip}, {fixture}, {lifetime}, {suite},
//                     {async_wrapper}, {is_async}, {items_per_call}, {owner}, {measured_wrapper}
//   array_decl_*:     {name}; or {count}, {name}, {body}
//   forward_decl_*:   {name}; or {scope}, {lines}
//
// IMPORTANT: Partials are fed into fmt::format, so any literal braces that
// must appear in the generated C++ need to be doubled here (e.g. function
// bodies, initializer lists, random_device{{}}).

#pragma once

#include <string_view>

namespace gentest::codegen::tpl {

inline constexpr std::string_view registration_preamble_light = R"CPP(#include <array>
#include <span>
#include <string_view>

#include "gentest/detail/registration_runtime.h"

#if !defined(GENTEST_CASE_API_VERSION) || GENTEST_CASE_API_VERSION < 7 || \
    !defined(GENTEST_CASE_API_HAS_OWNER) || !GENTEST_CASE_API_HAS_OWNER
#error "gentest_codegen output requires gentest headers with Case::is_death; use matching gentest headers/runtime"
#endif
#if !defined(GENTEST_BENCHMARK_API_VERSION) || GENTEST_BENCHMARK_API_VERSION < 1
#error "gentest_codegen output requires benchmark API version 1; use matching gentest headers/runtime"
#endif
)CPP";
;

inline constexpr std::string_view registration_preamble_full = R"CPP(#include <array>
#include <chrono>
#include <fmt/format.h>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "gentest/async.h"
#include "gentest/detail/generated_runtime.h"

#if !defined(GENTEST_CASE_API_VERSION) || GENTEST_CASE_API_VERSION < 7 || \
    !defined(GENTEST_CASE_API_HAS_OWNER) || !GENTEST_CASE_API_HAS_OWNER
#error "gentest_codegen output requires gentest headers with Case::is_death; use matching gentest headers/runtime"
#endif
#if !defined(GENTEST_BENCHMARK_API_VERSION) || GENTEST_BENCHMARK_API_VERSION < 1
#error "gentest_codegen output requires benchmark API version 1; use matching gentest headers/runtime"
#endif
)CPP";
;

inline constexpr std::string_view wrapper_support_common = R"CPP(
namespace {
template <typename T>
[[maybe_unused]] inline void gentest_maybe_setup(T& t) {
    if constexpr (std::is_base_of_v<gentest::FixtureSetup, T>) {
        ::gentest::detail::FrameworkPhaseScope events(::gentest::FrameworkPhase::setup, true);
        t.setUp();
    }
}

template <typename T>
[[maybe_unused]] inline void gentest_maybe_teardown(T& t) {
    if constexpr (std::is_base_of_v<gentest::FixtureTearDown, T>) {
        ::gentest::detail::FrameworkPhaseScope events(::gentest::FrameworkPhase::teardown, true);
        t.tearDown();
    }
}

template <typename T>
[[maybe_unused]] inline ::gentest::awaitable<void> gentest_maybe_async_setup(T& t) {
    if constexpr (std::is_base_of_v<gentest::AsyncFixtureSetup, T>) {
        ::gentest::detail::FrameworkPhaseScope events(::gentest::FrameworkPhase::setup, true);
        co_await t.setUp();
    } else if constexpr (std::is_base_of_v<gentest::FixtureSetup, T>) {
        ::gentest::detail::FrameworkPhaseScope events(::gentest::FrameworkPhase::setup, true);
        t.setUp();
    }
    co_return;
}

template <typename T>
[[maybe_unused]] inline ::gentest::awaitable<void> gentest_maybe_async_teardown(T& t) {
    if constexpr (std::is_base_of_v<gentest::AsyncFixtureTearDown, T>) {
        ::gentest::detail::FrameworkPhaseScope events(::gentest::FrameworkPhase::teardown, true);
        co_await t.tearDown();
    } else if constexpr (std::is_base_of_v<gentest::FixtureTearDown, T>) {
        ::gentest::detail::FrameworkPhaseScope events(::gentest::FrameworkPhase::teardown, true);
        t.tearDown();
    }
    co_return;
}

[[maybe_unused]] inline void gentest_record_fixture_failure(std::string_view fixture, std::string_view reason) {
    std::string msg =
        reason.empty() ? fmt::format("fixture allocation failed for '{}'", fixture) : fmt::format("fixture allocation failed for '{}': {}", fixture, reason);
    if (::gentest::detail::bench_phase() != ::gentest::detail::BenchPhase::None) {
        ::gentest::detail::record_bench_error(std::move(msg));
        return;
    }
    gentest::detail::record_failure(std::move(msg));
}

// A body exception stays pending until every armed local hook has run.
struct gentest_local_cleanup {
#if GENTEST_EXCEPTIONS_ENABLED
    std::exception_ptr pending;
    bool body_failed = false;

    void capture_body_exception() {
        pending = std::current_exception();
        try {
            throw;
        } catch (const ::gentest::detail::skip_exception &) {
            ::gentest::detail::preserve_local_fixture_skip();
        } catch (...) {
            body_failed = true;
        }
    }

    void capture_teardown_exception() {
        // Keep the original body/setup exception as the primary diagnostic.
        if (body_failed) return;

        try {
            throw;
        } catch (const ::gentest::detail::skip_exception &) {
            if (!pending) {
                pending = std::current_exception();
                ::gentest::detail::preserve_local_fixture_skip();
            }
        } catch (const ::gentest::assertion &) {
            // Fatal assertions already carry a recorded diagnostic.
            if (!pending) pending = std::current_exception();
        } catch (const std::exception &e) {
            ::gentest::detail::record_failure(fmt::format("fixture teardown failed: {}", e.what()));
        } catch (...) {
            ::gentest::detail::record_failure("fixture teardown failed: unknown exception");
        }
    }
#endif

    void rethrow_pending() const {
#if GENTEST_EXCEPTIONS_ENABLED
        if (pending) std::rethrow_exception(pending);
#endif
    }

    template <typename T> [[maybe_unused]] void run(T &fixture) {
#if GENTEST_EXCEPTIONS_ENABLED
        try { gentest_maybe_teardown(fixture); } catch (...) { capture_teardown_exception(); }
#else
        gentest_maybe_teardown(fixture);
#endif
    }

    template <typename T> [[maybe_unused]] ::gentest::awaitable<void> run_async(T &fixture) {
#if GENTEST_EXCEPTIONS_ENABLED
        try { co_await gentest_maybe_async_teardown(fixture); } catch (...) { capture_teardown_exception(); }
#else
        co_await gentest_maybe_async_teardown(fixture);
#endif
    }
};

template <typename TeardownFn>
struct gentest_noexceptions_local_teardown {
    TeardownFn *teardown = nullptr;
    bool        ran      = false;

    static void run(void *user_data) noexcept {
        auto *state = static_cast<gentest_noexceptions_local_teardown *>(user_data);
        if (!state || state->ran || !state->teardown) return;
        state->ran = true;
        (*state->teardown)();
    }

    void run_now() noexcept { run(this); }
};

template <typename BodyFn, typename TeardownFn>
[[maybe_unused]] inline void gentest_run_with_local_teardown(BodyFn &&body, TeardownFn &&teardown) {
    gentest_local_cleanup cleanup;
#if GENTEST_EXCEPTIONS_ENABLED
    try {
        body();
    } catch (...) {
        cleanup.capture_body_exception();
    }
    teardown(cleanup);
    cleanup.rethrow_pending();
#else
    auto teardown_fn = [&] { teardown(cleanup); };
    gentest_noexceptions_local_teardown<decltype(teardown_fn)> teardown_state{&teardown_fn};
    ::gentest::detail::NoExceptionsFatalHookScope            fatal_scope(&decltype(teardown_state)::run, &teardown_state);
    body();
    teardown_state.run_now();
#endif
}

template <typename TeardownFn>
struct gentest_async_local_teardown_guard {
    TeardownFn                                  *teardown = nullptr;
    ::gentest::detail::ContextCancelHookToken    cancel_hook;
    bool                                         ran      = false;

    explicit gentest_async_local_teardown_guard(TeardownFn *teardown_fn)
        : teardown(teardown_fn),
          cancel_hook(::gentest::detail::register_context_cancel_hook(::gentest::detail::ContextCancelHookState{&run, this})) {}

    gentest_async_local_teardown_guard(const gentest_async_local_teardown_guard &)            = delete;
    gentest_async_local_teardown_guard &operator=(const gentest_async_local_teardown_guard &) = delete;

    ~gentest_async_local_teardown_guard() {
        ::gentest::detail::unregister_context_cancel_hook(cancel_hook);
        run_blocking();
    }

    static void run(void *user_data) noexcept {
        auto *state = static_cast<gentest_async_local_teardown_guard *>(user_data);
        if (!state) return;
        state->run_blocking();
    }

    ::gentest::awaitable<void> run_now() {
        if (!ran && teardown) {
            ran = true;
            co_await (*teardown)();
        }
        co_return;
    }

    void run_blocking() noexcept {
        if (ran || !teardown) return;
        std::string error;
        if (!::gentest::detail::run_async_task_blocking(run_now(), "async local fixture teardown", error)) {
            std::string msg = "async local fixture teardown failed";
            if (!error.empty()) {
                msg += ": ";
                msg += error;
            }
            ::gentest::detail::record_failure(std::move(msg));
        }
    }

};

template <typename BodyFn, typename TeardownFn>
[[maybe_unused]] inline ::gentest::awaitable<void> gentest_run_async_with_local_teardown(BodyFn &&body, TeardownFn &&teardown) {
    gentest_local_cleanup cleanup;
    auto teardown_fn = [&]() -> ::gentest::awaitable<void> {
        co_await teardown(cleanup);
        cleanup.rethrow_pending();
    };
    gentest_async_local_teardown_guard<decltype(teardown_fn)> teardown_state{&teardown_fn};
#if GENTEST_EXCEPTIONS_ENABLED
    try {
        co_await body();
    } catch (...) {
        cleanup.capture_body_exception();
    }
#else
    ::gentest::detail::NoExceptionsFatalHookScope fatal_scope(&decltype(teardown_state)::run, &teardown_state);
    co_await body();
#endif
    co_await teardown_state.run_now();
    co_return;
}

template <typename Handle>
[[maybe_unused]] inline bool gentest_init_fixture(Handle& handle, std::string_view fixture) {
    if (!handle.init()) {
        gentest_record_fixture_failure(fixture, "returned null");
        return false;
    }
    return true;
}

template <typename T>
[[maybe_unused]] inline bool gentest_init_shared_fixture(::gentest::detail::FixtureHandle<T>& handle,
                                        ::gentest::detail::SharedFixtureScope scope,
                                        std::string_view suite,
                                        std::string_view fixture) {
    std::string reason;
    auto shared = ::gentest::detail::get_shared_fixture_typed<T>(scope, suite, fixture, reason);
    if (!shared) {
        // Keep this path in its templated caller: registration units without
        // shared free-function fixtures do not need a standalone helper.
        std::string msg =
            reason.empty() ? fmt::format("shared fixture unavailable for '{}'", fixture) : fmt::format("shared fixture unavailable for '{}': {}", fixture, reason);
        if constexpr (::gentest::detail::exceptions_enabled) {
            ::gentest::detail::skip_shared_fixture_unavailable(msg);
        } else if (::gentest::detail::bench_phase() != ::gentest::detail::BenchPhase::None) {
            ::gentest::detail::record_bench_error(std::move(msg));
        } else {
            ::gentest::detail::record_failure(std::move(msg));
        }
        return false;
    }
    if (!handle.init_shared(std::move(shared))) {
        gentest_record_fixture_failure(fixture, "returned null");
        return false;
    }
    return true;
}
} // namespace
)CPP";
;

inline constexpr std::string_view registration_common = R"CPP(
{{FORWARD_DECLS}}

namespace {

{{TRAIT_DECLS}}
)CPP";
;

inline constexpr std::string_view tu_registration_header = R"CPP(// This file is auto-generated by gentest_codegen.
// Do not edit manually.

#pragma once

#if !defined(GENTEST_TU_REGISTRATION_HEADER_NO_PREAMBLE)
{{REGISTRATION_PREAMBLE}}
#endif
{{WRAPPER_SUPPORT_COMMON}}
{{GLOBAL_WRAPPER_IMPLS}}
namespace gentest::generated::{{REGISTER_FN}} {
{{REGISTRATION_COMMON}}

constexpr std::array<gentest::Case, {{CASE_COUNT}}> kCases = {
{{CASE_INITS}}
};

// The slot name is only unique within one target. Keep the registrar in this
// unnamed namespace so its inline constructor has internal linkage; otherwise
// two codegen-attached targets linked into one program would define the same
// external constructor symbol and the linker would keep only one of them.
struct {{REGISTER_FN}}_registrar {
    {{REGISTER_FN}}_registrar() {
{{FIXTURE_REGISTRATIONS}}        gentest::detail::register_cases(std::span{kCases});
    }
};
[[maybe_unused]] const {{REGISTER_FN}}_registrar {{REGISTER_FN}}_instance{};
} // namespace
} // namespace gentest::generated::{{REGISTER_FN}}
)CPP";
;

inline constexpr std::string_view wrapper_free_test = R"FMT(static void {w}(void* ctx_) {{
    (void)ctx_;
    {invoke}
}}

)FMT";

inline constexpr std::string_view wrapper_free = R"FMT(static void {w}(void* ctx_) {{
    (void)ctx_;
    const auto phase = ::gentest::detail::bench_phase();
    if (phase != ::gentest::detail::BenchPhase::None) {{
        if (phase == ::gentest::detail::BenchPhase::Call) {{
            {invoke}
        }}
        return;
    }}
    {invoke}
}}

)FMT";

inline constexpr std::string_view wrapper_free_fixtures = R"FMT(static auto& {w}_bench_state() {{
    struct BenchState {{
{bench_decls}{bench_setup_flags}        bool ready = false;
    }};
    static thread_local BenchState state{{}};
    return state;
}}

static void {w}(void* ctx_) {{
    (void)ctx_;
    const auto phase = ::gentest::detail::bench_phase();
    if (phase != ::gentest::detail::BenchPhase::None) {{
        auto& bench_state = {w}_bench_state();
        if (phase == ::gentest::detail::BenchPhase::Setup) {{
            bench_state = {{}};
{bench_inits}{bench_setup}            bench_state.ready = true;
            return;
        }}
        if (phase == ::gentest::detail::BenchPhase::Teardown) {{
            gentest_local_cleanup cleanup;
{bench_teardown}
            bench_state = {{}};
            cleanup.rethrow_pending();
            return;
        }}
        if (phase == ::gentest::detail::BenchPhase::Call) {{
            if (!bench_state.ready) return;
            {bench_invoke}
            return;
        }}
        return;
    }}
{decls}{inits}{setup_flags}    gentest_run_with_local_teardown(
        [&] {{
{setup}            {invoke}
        }},
        [&]([[maybe_unused]] gentest_local_cleanup &cleanup) {{
{teardown}        }});
}}

)FMT";

inline constexpr std::string_view wrapper_ephemeral = R"FMT(static void {w}(void* ctx_) {{
    (void)ctx_;
    const auto phase = ::gentest::detail::bench_phase();
    if (phase != ::gentest::detail::BenchPhase::None) {{
        struct BenchState {{
            ::gentest::detail::FixtureHandle<{fixture}> fx_{{::gentest::detail::FixtureHandle<{fixture}>::empty()}};
            bool teardown_armed = false;
            bool ready = false;
        }};
        static thread_local BenchState bench_state{{}};
        if (phase == ::gentest::detail::BenchPhase::Setup) {{
            bench_state = BenchState{{}};
            if (!gentest_init_fixture(bench_state.fx_, "{fixture}")) return;
            bench_state.teardown_armed = true;
            gentest_maybe_setup(bench_state.fx_.ref());
            bench_state.ready = true;
            return;
        }}
        if (phase == ::gentest::detail::BenchPhase::Teardown) {{
            gentest_local_cleanup cleanup;
            if (bench_state.teardown_armed) cleanup.run(bench_state.fx_.ref());
            bench_state = BenchState{{}};
            cleanup.rethrow_pending();
            return;
        }}
        if (phase == ::gentest::detail::BenchPhase::Call) {{
            if (!bench_state.ready) return;
            {bench_invoke}
            return;
        }}
        return;
    }}
    auto fx_ = ::gentest::detail::FixtureHandle<{fixture}>::empty();
    if (!gentest_init_fixture(fx_, "{fixture}")) return;
    bool fx_teardown_armed = false;
    gentest_run_with_local_teardown(
        [&] {{
            fx_teardown_armed = true;
            gentest_maybe_setup(fx_.ref());
            {invoke}
        }},
        [&](gentest_local_cleanup &cleanup) {{
            if (fx_teardown_armed) cleanup.run(fx_.ref());
        }});
}}

)FMT";

inline constexpr std::string_view wrapper_stateful = R"FMT(static void {w}(void* ctx_) {{
    auto* fx_ = static_cast<{fixture}*>(ctx_);
    if (!fx_) {{
        gentest_record_fixture_failure("{fixture}", "instance missing");
        return;
    }}
    const auto phase = ::gentest::detail::bench_phase();
    if (phase != ::gentest::detail::BenchPhase::None) {{
        if (phase == ::gentest::detail::BenchPhase::Setup) {{
            return;
        }}
        if (phase == ::gentest::detail::BenchPhase::Teardown) {{
            return;
        }}
        if (phase == ::gentest::detail::BenchPhase::Call) {{
            {invoke}
            return;
        }}
        return;
    }}
    {invoke}
}}

)FMT";

inline constexpr std::string_view case_entry = R"FMT(    gentest::Case{{
        .name = "{name}",
        .fn = &{wrapper},
        .file = "{file}",
        .line = {line},
        .is_benchmark = {is_bench},
        .is_jitter = {is_jitter},
        .is_baseline = {is_baseline},
        .tags = std::span{{{tags}}},
        .requirements = std::span{{{reqs}}},
        .skip_reason = {skip_reason},
        .should_skip = {should_skip},
        .fixture = {fixture},
        .fixture_lifetime = {lifetime},
        .suite = {suite},
        .async_fn = {async_wrapper},
        .is_async = {is_async},
        .items_per_call = {items_per_call},
        .owner = {owner},
        .benchmark = {{
            .execution = {benchmark_execution},
            .time_source = {benchmark_time_source},
            .warmup_epochs = {benchmark_warmup}
        }},
        .reports_body_start = true,
        .measured_fn = {measured_wrapper},
        .is_death = {is_death}
    }},

)FMT";

inline constexpr std::string_view array_decl_empty = R"FMT(constexpr std::array<std::string_view, 0> {name}{{}};

)FMT";

inline constexpr std::string_view array_decl_nonempty = R"FMT(constexpr std::array<std::string_view, {count}> {name} = {{
{body}
}};

)FMT";

inline constexpr std::string_view forward_decl_line = R"FMT(void {name}();

)FMT";

inline constexpr std::string_view forward_decl_ns = R"FMT(namespace {scope} {{
{lines}}} // namespace {scope}

)FMT";

} // namespace gentest::codegen::tpl
