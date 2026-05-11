#pragma once

#include "gentest/mock_fwd.h"
#include "third_party_inherited_public_pure_service.hpp"

namespace fixture::validation::aliases {

template <typename T> struct InheritedPublicWrap {
    using type = T;
};

template <typename T> using InheritedPublicAlias = typename InheritedPublicWrap<T>::type;

using InheritedPublicPureTarget = InheritedPublicAlias<InheritedPublicPureService>;

} // namespace fixture::validation::aliases

namespace fixture::validation::mocks {

using InheritedPublicPureServiceMock = gentest::mock<aliases::InheritedPublicPureTarget>;

} // namespace fixture::validation::mocks
