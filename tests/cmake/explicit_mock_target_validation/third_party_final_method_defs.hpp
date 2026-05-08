#pragma once

#include "gentest/mock_fwd.h"
#include "third_party_final_method_service.hpp"

namespace fixture::validation::aliases {

template <typename T> using Alias = T;

using FinalMethodTarget = Alias<FinalMethodService>;

} // namespace fixture::validation::aliases

namespace fixture::validation::mocks {

using FinalMethodServiceMock = gentest::mock<aliases::FinalMethodTarget>;

} // namespace fixture::validation::mocks
