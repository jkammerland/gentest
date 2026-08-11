#pragma once

// Optional C++26 reflection registration. This header remains includable on
// older language modes; the reflection-only declarations are exposed only
// when both the core language and <meta> feature-test macros are present.

#if defined(__cpp_impl_reflection) && defined(__has_include)
#if __has_include(<meta>)
#include <meta>
#endif
#endif

#if defined(__cpp_impl_reflection) && defined(__cpp_lib_reflection)
#define GENTEST_HAS_CXX26_REFLECTION 1
#else
#define GENTEST_HAS_CXX26_REFLECTION 0
#endif

namespace gentest::reflection {

inline constexpr bool available = GENTEST_HAS_CXX26_REFLECTION != 0;

} // namespace gentest::reflection

#if GENTEST_HAS_CXX26_REFLECTION

#include "gentest/detail/registration_runtime.h"
#include "gentest/detail/runtime_support.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace gentest::reflection {

enum class CaseKind {
    Test,
    Benchmark,
    Jitter,
};

template <std::size_t Capacity> struct FixedText {
    std::array<char, Capacity + 1> characters{};
    std::size_t                    length{};

    constexpr FixedText() = default;

    template <std::size_t Size> consteval explicit FixedText(const char (&text)[Size]) { assign(text); }

    template <std::size_t Size> consteval void assign(const char (&text)[Size]) {
        static_assert(Size > 0);
        if (Size - 1 > Capacity)
            throw "gentest reflection annotation text exceeds its fixed capacity";

        length = Size - 1;
        for (std::size_t index = 0; index < Size; ++index)
            characters[index] = text[index];
    }

    [[nodiscard]] constexpr auto view() const -> std::string_view { return {characters.data(), length}; }
};

struct CaseSpec {
    static constexpr std::size_t maxNameLength     = 255;
    static constexpr std::size_t maxMetadataLength = 127;
    static constexpr std::size_t maxTags           = 8;
    static constexpr std::size_t maxRequirements   = 8;

    CaseKind                                                  kind{CaseKind::Test};
    FixedText<maxNameLength>                                  name{};
    bool                                                      isBaseline{};
    std::uint64_t                                             measuredItemsPerCall{1};
    std::array<FixedText<maxMetadataLength>, maxTags>         tags{};
    std::size_t                                               tagCount{};
    std::array<FixedText<maxMetadataLength>, maxRequirements> requirements{};
    std::size_t                                               requirementCount{};
    FixedText<maxMetadataLength>                              skipReason{};
    FixedText<maxMetadataLength>                              caseOwner{};

    template <std::size_t Size> consteval CaseSpec(CaseKind caseKind, const char (&caseName)[Size]) : kind(caseKind), name(caseName) {
        if (Size <= 1)
            throw "gentest reflection case names must not be empty";
    }

    template <std::size_t Size> [[nodiscard]] consteval auto tag(const char (&value)[Size]) const -> CaseSpec {
        auto result = *this;
        if (result.tagCount == maxTags)
            throw "gentest reflection cases support at most eight tags";
        result.tags[result.tagCount++].assign(value);
        return result;
    }

    template <std::size_t Size> [[nodiscard]] consteval auto requirement(const char (&value)[Size]) const -> CaseSpec {
        auto result = *this;
        if (result.requirementCount == maxRequirements)
            throw "gentest reflection cases support at most eight requirements";
        result.requirements[result.requirementCount++].assign(value);
        return result;
    }

    template <std::size_t Size> [[nodiscard]] consteval auto skip(const char (&reason)[Size]) const -> CaseSpec {
        auto result = *this;
        result.skipReason.assign(reason);
        return result;
    }

    template <std::size_t Size> [[nodiscard]] consteval auto owner(const char (&value)[Size]) const -> CaseSpec {
        auto result = *this;
        result.caseOwner.assign(value);
        return result;
    }

    [[nodiscard]] consteval auto baseline() const -> CaseSpec {
        if (kind == CaseKind::Test)
            throw "gentest reflection baseline() is valid only for benchmarks and jitters";
        auto result       = *this;
        result.isBaseline = true;
        return result;
    }

    [[nodiscard]] consteval auto itemsPerCall(std::uint64_t count) const -> CaseSpec {
        if (kind == CaseKind::Test)
            throw "gentest reflection itemsPerCall() is valid only for benchmarks and jitters";
        if (count == 0)
            throw "gentest reflection itemsPerCall() requires a non-zero count";
        auto result                 = *this;
        result.measuredItemsPerCall = count;
        return result;
    }
};

template <std::size_t Size> [[nodiscard]] consteval auto test(const char (&name)[Size]) -> CaseSpec { return {CaseKind::Test, name}; }

template <std::size_t Size> [[nodiscard]] consteval auto bench(const char (&name)[Size]) -> CaseSpec { return {CaseKind::Benchmark, name}; }

template <std::size_t Size> [[nodiscard]] consteval auto jitter(const char (&name)[Size]) -> CaseSpec { return {CaseKind::Jitter, name}; }

namespace detail {

template <std::meta::info Member> consteval auto caseSpecCount() -> std::size_t {
    return std::meta::annotations_of_with_type(Member, ^^CaseSpec).size();
}

template <std::meta::info Member> consteval auto extractCaseSpec() -> CaseSpec {
    const auto annotations = std::meta::annotations_of_with_type(Member, ^^CaseSpec);
    if (annotations.size() != 1)
        throw "a reflected gentest case requires exactly one CaseSpec annotation";
    return std::meta::extract<CaseSpec>(annotations.front());
}

template <std::meta::info Member> inline constexpr CaseSpec storedCaseSpec = extractCaseSpec<Member>();

template <std::meta::info Member>
inline constexpr auto tagViews = [] {
    std::array<std::string_view, CaseSpec::maxTags> result{};
    for (std::size_t index = 0; index < storedCaseSpec<Member>.tagCount; ++index)
        result[index] = storedCaseSpec<Member>.tags[index].view();
    return result;
}();

template <std::meta::info Member>
inline constexpr auto requirementViews = [] {
    std::array<std::string_view, CaseSpec::maxRequirements> result{};
    for (std::size_t index = 0; index < storedCaseSpec<Member>.requirementCount; ++index)
        result[index] = storedCaseSpec<Member>.requirements[index].view();
    return result;
}();

template <std::meta::info Member> constexpr auto caseSuite() -> std::string_view {
    const auto name      = storedCaseSpec<Member>.name.view();
    const auto separator = name.rfind('/');
    return separator == std::string_view::npos ? std::string_view{} : name.substr(0, separator);
}

template <std::meta::info Function> void invoke(void *context) {
    (void)context;
    using FunctionType = decltype([:Function:]);
    static_assert(std::is_invocable_v<FunctionType>, "reflected gentest cases must be callable without arguments");
    static_assert(std::is_same_v<std::invoke_result_t<FunctionType>, void>, "reflected gentest cases must return void");

    if constexpr (storedCaseSpec<Function>.kind != CaseKind::Test) {
        const auto phase = ::gentest::detail::bench_phase();
        if (phase != ::gentest::detail::BenchPhase::None) {
            if (phase == ::gentest::detail::BenchPhase::Call)
                [:Function:]();
            return;
        }
    }
    [:Function:]();
}

template <std::meta::info Member> auto makeCase() -> Case {
    constexpr auto location = std::meta::source_location_of(Member);
    constexpr auto kind     = storedCaseSpec<Member>.kind;
    return {
        .name             = storedCaseSpec<Member>.name.view(),
        .fn               = &invoke<Member>,
        .file             = location.file_name(),
        .line             = location.line(),
        .is_benchmark     = kind == CaseKind::Benchmark,
        .is_jitter        = kind == CaseKind::Jitter,
        .is_baseline      = storedCaseSpec<Member>.isBaseline,
        .tags             = std::span{tagViews<Member>.data(), storedCaseSpec<Member>.tagCount},
        .requirements     = std::span{requirementViews<Member>.data(), storedCaseSpec<Member>.requirementCount},
        .skip_reason      = storedCaseSpec<Member>.skipReason.view(),
        .should_skip      = !storedCaseSpec<Member>.skipReason.view().empty(),
        .fixture_lifetime = FixtureLifetime::None,
        .suite            = caseSuite<Member>(),
        .items_per_call   = storedCaseSpec<Member>.measuredItemsPerCall,
        .owner            = storedCaseSpec<Member>.caseOwner.view(),
    };
}

template <std::meta::info Member> void appendCase(std::vector<Case> &cases) {
    if constexpr (caseSpecCount<Member>() != 0) {
        static_assert(caseSpecCount<Member>() == 1, "a reflected gentest case cannot have multiple CaseSpec annotations");
        static_assert(std::meta::is_function(Member), "CaseSpec annotations can be applied only to non-template free functions");
        cases.push_back(makeCase<Member>());
    }
}

template <std::meta::info Scope, typename TranslationUnitTag> consteval auto scopeMemberCount() -> std::size_t {
    return std::meta::members_of(Scope, std::meta::access_context::current()).size();
}

template <std::meta::info Scope, typename TranslationUnitTag, std::size_t Index> consteval auto scopeMember() -> std::meta::info {
    return std::meta::members_of(Scope, std::meta::access_context::current())[Index];
}

template <std::meta::info Scope, typename TranslationUnitTag, std::size_t... Indices>
auto makeScopeCases(std::index_sequence<Indices...>) -> std::vector<Case> {
    std::vector<Case> cases;
    cases.reserve(sizeof...(Indices));
    (appendCase<scopeMember<Scope, TranslationUnitTag, Indices>()>(cases), ...);
    return cases;
}

template <std::meta::info Scope, typename TranslationUnitTag> auto scopeCases() -> const std::vector<Case> & {
    static const auto cases =
        makeScopeCases<Scope, TranslationUnitTag>(std::make_index_sequence<scopeMemberCount<Scope, TranslationUnitTag>()>{});
    return cases;
}

template <std::meta::info Scope, typename TranslationUnitTag> struct ScopeRegistrar {
    ScopeRegistrar() {
        const auto &cases = scopeCases<Scope, TranslationUnitTag>();
        ::gentest::detail::register_cases(std::span<const Case>{cases.data(), cases.size()});
    }
};

} // namespace detail
} // namespace gentest::reflection

#define GENTEST_DETAIL_REFLECTION_JOIN_IMPL(lhs, rhs) lhs##rhs
#define GENTEST_DETAIL_REFLECTION_JOIN(lhs, rhs)      GENTEST_DETAIL_REFLECTION_JOIN_IMPL(lhs, rhs)
#define GENTEST_DETAIL_REFLECTION_REGISTER_IMPL(scope, line)                                                                               \
    namespace {                                                                                                                            \
    struct GENTEST_DETAIL_REFLECTION_JOIN(GentestReflectionTranslationUnitTag, line) {};                                                   \
    [[maybe_unused]] const ::gentest::reflection::detail::ScopeRegistrar<                                                                  \
        ^^scope, GENTEST_DETAIL_REFLECTION_JOIN(GentestReflectionTranslationUnitTag, line)>                                                \
        GENTEST_DETAIL_REFLECTION_JOIN(gentestReflectionRegistrar, line){};                                                                \
    }
#define GENTEST_REFLECT_REGISTER(scope) GENTEST_DETAIL_REFLECTION_REGISTER_IMPL(scope, __LINE__)

#else

#define GENTEST_REFLECT_REGISTER(scope)                                                                                                    \
    static_assert(::gentest::reflection::available,                                                                                        \
                  "GENTEST_REFLECT_REGISTER requires C++26 reflection and a standard library that provides <meta>")

#endif
