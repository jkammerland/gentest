#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::FinalMethodServiceMock mock;
    fixture::validation::FinalMethodService           *service = &mock;
    return service->compute(service->stable(4));
}
