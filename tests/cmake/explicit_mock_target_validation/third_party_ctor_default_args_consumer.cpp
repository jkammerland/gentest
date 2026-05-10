#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::ThirdPartyCtorDefaultArgsServiceMock mock{6};
    return mock.seed == 12 ? 0 : 1;
}
