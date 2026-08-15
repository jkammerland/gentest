#pragma once

#include "gentest/attributes.h"

namespace manifest_fixture {

struct [[using gentest: fixture(global)]] SharedFixture {
    int value = 7;
};

[[using gentest: test("manifest/header_shared_fixture")]]
void header_shared_fixture(SharedFixture &fixture);

} // namespace manifest_fixture
