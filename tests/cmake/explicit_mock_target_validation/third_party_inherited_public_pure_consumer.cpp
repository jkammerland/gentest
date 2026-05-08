#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::InheritedPublicPureServiceMock mock;
    fixture::validation::InheritedPublicPureService           *service = &mock;
    return service->inherited(3) + service->visible(5);
}
