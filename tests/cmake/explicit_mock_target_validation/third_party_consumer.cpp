#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::ThirdPartyServiceMock mock;
    fixture::validation::ThirdPartyService           *service = &mock;
    return service->compute(7);
}
