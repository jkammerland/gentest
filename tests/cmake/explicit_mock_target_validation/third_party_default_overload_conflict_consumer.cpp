#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::DefaultOverloadConflictServiceMock mock;
    return mock.compute(1, 2);
}
