#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::ThirdPartyMethodDefaultArgsServiceMock mock;
    return mock.compute();
}
