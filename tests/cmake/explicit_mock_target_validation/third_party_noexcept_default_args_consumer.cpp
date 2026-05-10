#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::NoexceptDefaultArgsServiceMock mock;
    static_assert(!noexcept(mock.compute()));
    return mock.compute();
}
