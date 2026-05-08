#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::VariadicServiceMock mock;
    fixture::validation::VariadicService           *service = &mock;
    return service->log("%d", 7);
}
