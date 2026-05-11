#include "public/fixture_gmock_mocks.hpp"

int main() {
    fixture::mocks::ServiceMock mock;
    fixture::Service           *service = &mock;
    return service->compute(0);
}
