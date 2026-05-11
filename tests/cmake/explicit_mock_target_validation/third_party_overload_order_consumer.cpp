#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::OverloadOrderServiceMock mock;
    fixture::validation::OverloadOrderService           *service = &mock;
    return service->value(1L) + service->value(2.0) + service->value(3) + service->value(4.0F);
}
