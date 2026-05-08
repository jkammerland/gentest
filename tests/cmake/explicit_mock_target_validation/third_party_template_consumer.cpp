#include "public/fixture_validation.hpp"

int main() {
    fixture::validation::mocks::TemplateRepoIntMock mock;
    fixture::validation::TemplateRepo<int>         *repo = &mock;
    return repo->load(7);
}
