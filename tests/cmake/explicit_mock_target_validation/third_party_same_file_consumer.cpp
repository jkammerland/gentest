#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::SameFileServiceMock mock;
    return mock.compute(7);
}
