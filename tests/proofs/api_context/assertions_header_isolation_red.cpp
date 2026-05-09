#include "gentest/assertions.h"
#include "gentest/context.h"

#include <type_traits>

namespace {

template <typename T, typename = void> inline constexpr bool kCompleteType                                      = false;
template <typename T> inline constexpr bool                  kCompleteType<T, std::void_t<decltype(sizeof(T))>> = true;

static_assert(!kCompleteType<gentest::detail::TestContextInfo>,
              "gentest/assertions.h + gentest/context.h must not expose the concrete TestContextInfo layout");
static_assert(std::is_same_v<decltype(gentest::expect(true)), void>, "gentest/assertions.h should expose expectation helpers");

} // namespace

int main() { return 0; }
