#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::OperatorServiceMock mock;
    fixture::validation::OperatorService           *service = &mock;
    return (*service == mock) ? 0 : 1;
}
