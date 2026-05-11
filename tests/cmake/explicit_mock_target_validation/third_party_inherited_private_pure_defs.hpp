#pragma once

#include "gentest/mock_fwd.h"
#include "third_party_inherited_private_pure_service.hpp"

namespace fixture::validation::aliases {

template <typename T> struct InheritedPrivateWrap {
    using type = T;
};

template <typename T> using InheritedPrivateAlias = typename InheritedPrivateWrap<T>::type;

using InheritedPrivatePureTarget = InheritedPrivateAlias<InheritedPrivatePureService>;

} // namespace fixture::validation::aliases

namespace fixture::validation::mocks {

using InheritedPrivatePureServiceMock = gentest::mock<aliases::InheritedPrivatePureTarget>;

} // namespace fixture::validation::mocks
