#pragma once

#include "gentest/mock_fwd.h"
#include "service.hpp"

namespace fixture::third_party::mock_defs {

template <typename T> struct SelectTarget {
    using type = T;
};

template <typename T> using SelectedTarget = typename SelectTarget<T>::type;

using CalculatorTarget      = SelectedTarget<Calculator>;
using ResourceFactoryTarget = typename SelectTarget<ResourceFactory>::type;

} // namespace fixture::third_party::mock_defs

namespace fixture::third_party::mocks {

using CalculatorMock      = gentest::mock<mock_defs::CalculatorTarget>;
using ResourceFactoryMock = gentest::mock<mock_defs::ResourceFactoryTarget>;

} // namespace fixture::third_party::mocks
