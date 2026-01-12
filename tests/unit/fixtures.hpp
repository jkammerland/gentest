#pragma once

#include "gentest/attributes.h"
#include "gentest/runner.h"

namespace unit {

struct DefaultNameFixture {
    [[using gentest: fast]]
    void default_name_member() {
        gentest::asserts::EXPECT_TRUE(true);
    }
};

} // namespace unit
