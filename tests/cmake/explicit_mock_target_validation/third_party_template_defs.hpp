#pragma once

#include "gentest/mock_fwd.h"
#include "third_party_template_service.hpp"

namespace fixture::validation::mocks {

using TemplateRepoIntMock = gentest::mock<TemplateRepo<int>>;

} // namespace fixture::validation::mocks
