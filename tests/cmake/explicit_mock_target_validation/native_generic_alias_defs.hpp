#pragma once

#include "native_generic_alias_service.hpp"

namespace fixture::validation::mocks {

template <typename T> using MockFor = gentest::mock<T>;

using GenericAliasServiceMock = MockFor<GenericAliasService>;

} // namespace fixture::validation::mocks
