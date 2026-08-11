#include "gentest/detail/case_api.h"
#include "gentest/test.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace {

template <typename T, typename = void> inline constexpr bool kCompleteType                                      = false;
template <typename T> inline constexpr bool                  kCompleteType<T, std::void_t<decltype(sizeof(T))>> = true;

struct CaseLayoutMirror {
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
    std::string_view                  fixture;
    gentest::FixtureLifetime          fixture_lifetime;
    std::string_view                  suite;
    gentest::detail::AsyncCaseFn      async_fn{nullptr};
    bool                              is_async{false};
    std::uint64_t                     items_per_call{1};
    std::string_view                  owner{};
};

static_assert(!kCompleteType<gentest::detail::AsyncTask>, "gentest/test.h and Case must not parse the full async implementation");
static_assert(std::is_same_v<decltype(gentest::Case::async_fn), gentest::detail::AsyncCaseFn>);
static_assert(std::is_standard_layout_v<gentest::Case>);
static_assert(sizeof(gentest::Case) == sizeof(CaseLayoutMirror));
static_assert(alignof(gentest::Case) == alignof(CaseLayoutMirror));
static_assert(offsetof(gentest::Case, name) == offsetof(CaseLayoutMirror, name));
static_assert(offsetof(gentest::Case, fn) == offsetof(CaseLayoutMirror, fn));
static_assert(offsetof(gentest::Case, file) == offsetof(CaseLayoutMirror, file));
static_assert(offsetof(gentest::Case, line) == offsetof(CaseLayoutMirror, line));
static_assert(offsetof(gentest::Case, is_benchmark) == offsetof(CaseLayoutMirror, is_benchmark));
static_assert(offsetof(gentest::Case, is_jitter) == offsetof(CaseLayoutMirror, is_jitter));
static_assert(offsetof(gentest::Case, is_baseline) == offsetof(CaseLayoutMirror, is_baseline));
static_assert(offsetof(gentest::Case, tags) == offsetof(CaseLayoutMirror, tags));
static_assert(offsetof(gentest::Case, requirements) == offsetof(CaseLayoutMirror, requirements));
static_assert(offsetof(gentest::Case, skip_reason) == offsetof(CaseLayoutMirror, skip_reason));
static_assert(offsetof(gentest::Case, should_skip) == offsetof(CaseLayoutMirror, should_skip));
static_assert(offsetof(gentest::Case, fixture) == offsetof(CaseLayoutMirror, fixture));
static_assert(offsetof(gentest::Case, fixture_lifetime) == offsetof(CaseLayoutMirror, fixture_lifetime));
static_assert(offsetof(gentest::Case, suite) == offsetof(CaseLayoutMirror, suite));
static_assert(offsetof(gentest::Case, async_fn) == offsetof(CaseLayoutMirror, async_fn));
static_assert(offsetof(gentest::Case, is_async) == offsetof(CaseLayoutMirror, is_async));
static_assert(offsetof(gentest::Case, items_per_call) == offsetof(CaseLayoutMirror, items_per_call));
static_assert(offsetof(gentest::Case, owner) == offsetof(CaseLayoutMirror, owner));

[[using gentest: test("narrow_surface/metadata"), fast, req("NARROW-1"), owner("headers"), skip("compile-only")]]
void synchronous_case_declaration() {}

} // namespace

int main() { return 0; }
