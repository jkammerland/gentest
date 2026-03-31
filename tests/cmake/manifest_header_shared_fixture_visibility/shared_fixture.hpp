#pragma once

namespace manifest_fixture {

struct [[using gentest: fixture(global)]] SharedFixture {
    int value = 7;
};

} // namespace manifest_fixture
