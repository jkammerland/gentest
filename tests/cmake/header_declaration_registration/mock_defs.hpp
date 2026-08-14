#pragma once

#include <gentest/mock.h>

namespace header_declaration_registration {

struct MockService {
    virtual ~MockService()      = default;
    virtual int compute(int in) = 0;
};

using MockServiceDouble = gentest::mock<MockService>;

} // namespace header_declaration_registration
