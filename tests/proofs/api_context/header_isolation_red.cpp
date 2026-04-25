#include "gentest/runner.h"

#include <memory>
#include <type_traits>

namespace {

template <typename T, typename = void> inline constexpr bool kCompleteType                                      = false;
template <typename T> inline constexpr bool                  kCompleteType<T, std::void_t<decltype(sizeof(T))>> = true;

static_assert(!kCompleteType<gentest::detail::TestContextInfo>, "gentest/runner.h must not expose the concrete TestContextInfo layout");
static_assert(std::is_same_v<decltype(gentest::get_current_context()), gentest::CurrentContext>,
              "gentest/runner.h should expose current-context capture through get_current_context");
static_assert(std::is_same_v<decltype(gentest::set_current_context(gentest::get_current_context())), gentest::Adoption>,
              "gentest/runner.h should expose current-context adoption through set_current_context");
static_assert(std::is_same_v<decltype(gentest::get_current_token()), gentest::CurrentToken>,
              "gentest/runner.h should keep the legacy current-token API");
static_assert(std::is_same_v<decltype(gentest::set_current_token(gentest::get_current_token())), gentest::Adoption>,
              "gentest/runner.h should keep legacy current-token adoption through set_current_token");

} // namespace

int main() { return 0; }
