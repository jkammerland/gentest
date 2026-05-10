#pragma once

namespace fixture::validation {

struct FinalPureService {
    virtual ~FinalPureService() = default;

    virtual int blocked() final = 0;
};

} // namespace fixture::validation

namespace fixture::validation::mocks {

using FinalPureServiceMock = gentest::mock<FinalPureService>;

} // namespace fixture::validation::mocks
