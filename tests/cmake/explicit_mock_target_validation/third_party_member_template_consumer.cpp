#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::MemberTemplateServiceMock mock;
    return mock.transform(7);
}
