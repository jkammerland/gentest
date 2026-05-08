#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::ConversionServiceMock mock;
    return static_cast<bool>(mock) ? 0 : 1;
}
