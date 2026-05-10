#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::DefaultArgTypeContextServiceMock mock;
    return mock.size() + mock.alignment() + mock.cast();
}
