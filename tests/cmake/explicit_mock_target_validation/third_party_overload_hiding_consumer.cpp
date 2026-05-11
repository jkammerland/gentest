#include "public/fixture_validation.hpp"

template <typename T>
concept CallsDeletedIntOverload = requires(T &mock) { mock.disabled(1); };

int main() {
    fixture::validation::mocks::OverloadHidingServiceMock mock;
    static_assert(!CallsDeletedIntOverload<decltype(mock)>);
    return mock.stable(9) == 9 ? 0 : 1;
}
