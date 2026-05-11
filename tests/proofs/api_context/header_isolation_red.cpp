#include "gentest/runner.h"

#include <memory>
#include <stop_token>
#include <type_traits>

namespace {

template <typename T, typename = void> inline constexpr bool kCompleteType                                      = false;
template <typename T> inline constexpr bool                  kCompleteType<T, std::void_t<decltype(sizeof(T))>> = true;
template <typename T>
concept kPointerLikeContext = requires(T context) { context.operator->(); };

static_assert(!kCompleteType<gentest::detail::TestContextInfo>, "gentest/runner.h must not expose the concrete TestContextInfo layout");
static_assert(std::is_same_v<decltype(gentest::get_current_context()), gentest::CurrentContext>,
              "gentest/runner.h should expose current-context capture through get_current_context");
static_assert(std::is_same_v<decltype(gentest::set_current_context(gentest::get_current_context())), gentest::CurrentContextLease>,
              "gentest/runner.h should expose current-context leasing through set_current_context");
static_assert(std::is_same_v<decltype(gentest::get_current_token()), gentest::CurrentToken>,
              "gentest/runner.h should keep the legacy current-token API");
static_assert(std::is_same_v<decltype(gentest::set_current_token(gentest::get_current_token())), gentest::CurrentContextLease>,
              "gentest/runner.h should keep legacy current-token leasing through set_current_token");
static_assert(std::is_same_v<decltype(gentest::get_current_context().stop_token()), std::stop_token>,
              "gentest::CurrentContext should expose cooperative stop through std::stop_token");
static_assert(std::is_same_v<decltype(gentest::get_current_context().stop_requested()), bool>,
              "gentest::CurrentContext should expose a stop_requested convenience query");
static_assert(!kPointerLikeContext<gentest::CurrentContext>,
              "gentest::CurrentContext must not expose TestContextInfo internals through pointer syntax");

} // namespace

int main() { return 0; }
