#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::StaticMethodServiceMock mock;
    return mock.compute(7);
}
