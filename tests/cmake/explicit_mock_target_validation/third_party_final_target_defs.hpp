#pragma once

#include "gentest/mock_fwd.h"
#include "third_party_final_target_service.hpp"

namespace fixture::validation::aliases {

template <typename T> struct Identity {
    using type = T;
};

using FinalTargetAlias = typename Identity<FinalTargetService>::type;

} // namespace fixture::validation::aliases

namespace fixture::validation::mocks {

using FinalTargetServiceMock = gentest::mock<aliases::FinalTargetAlias>;

} // namespace fixture::validation::mocks
