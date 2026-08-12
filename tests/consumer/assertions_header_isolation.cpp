#include "gentest/test.h"

#include <type_traits>

namespace {

template <typename T, typename = void> inline constexpr bool kCompleteType                                      = false;
template <typename T> inline constexpr bool                  kCompleteType<T, std::void_t<decltype(sizeof(T))>> = true;

static_assert(!kCompleteType<gentest::detail::TestContextInfo>,
              "installed gentest/test.h must not expose the concrete TestContextInfo layout");

} // namespace
