#pragma once

#include "gentest/mock_fwd.h"
#include "third_party_private_pure_service.hpp"

namespace fixture::validation::aliases {

template <typename T> struct Wrap {
    using type = T;
};

using PrivatePureTarget = typename Wrap<PrivatePureService>::type;

} // namespace fixture::validation::aliases

namespace fixture::validation::mocks {

using PrivatePureServiceMock = gentest::mock<aliases::PrivatePureTarget>;

} // namespace fixture::validation::mocks
