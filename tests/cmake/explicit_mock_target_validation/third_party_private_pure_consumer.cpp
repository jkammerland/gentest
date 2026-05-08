#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::PrivatePureServiceMock mock;
    fixture::validation::PrivatePureService           *service = &mock;
    return service->visible(5);
}
