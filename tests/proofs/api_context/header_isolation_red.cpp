#include "gentest/runner.h"

#include <memory>
#include <type_traits>

namespace {

template <typename T, typename = void> inline constexpr bool kCompleteType                                      = false;
template <typename T> inline constexpr bool                  kCompleteType<T, std::void_t<decltype(sizeof(T))>> = true;

static_assert(!kCompleteType<gentest::detail::TestContextInfo>, "gentest/runner.h must not expose the concrete TestContextInfo layout");
static_assert(std::is_same_v<decltype(gentest::ctx::current()), gentest::ctx::Token>,
              "gentest/runner.h should still expose the supported context token API");

} // namespace

int main() { return 0; }
