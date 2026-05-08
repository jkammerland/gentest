#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::FinalTargetServiceMock mock;
    return mock.compute(3);
}
