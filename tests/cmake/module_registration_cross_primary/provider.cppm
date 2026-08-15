module;

export module gentest.story116.fixture_provider;

import gentest;

export namespace story116 {

struct [[using gentest: fixture(suite)]] SharedState : gentest::FixtureSetup {
    void setUp() override {
        ++setup_count;
        value = 0;
    }

    int setup_count = 0;
    int value       = 0;
};

} // namespace story116
