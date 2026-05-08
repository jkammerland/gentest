#pragma once

namespace fixture::validation {

struct SameFileService {
    virtual ~SameFileService()     = default;
    virtual int compute(int value) = 0;
};

using SameFileServiceMock = gentest::mock<SameFileService>;

} // namespace fixture::validation
