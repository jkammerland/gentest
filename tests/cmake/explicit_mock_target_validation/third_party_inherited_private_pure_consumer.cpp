#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::InheritedPrivatePureServiceMock mock;
    fixture::validation::InheritedPrivatePureService           *service = &mock;
    return service->visible(7);
}
