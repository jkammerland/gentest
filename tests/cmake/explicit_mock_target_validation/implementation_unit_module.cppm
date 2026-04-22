module fixture.validation.implementation_unit_module;

import gentest.mock;

namespace fixture::validation {

struct ImplementationUnitService {
    virtual ~ImplementationUnitService() = default;
    virtual int value()                  = 0;
};

using ImplementationUnitServiceMock = gentest::mock<ImplementationUnitService>;

} // namespace fixture::validation
