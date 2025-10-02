#pragma once

namespace gentest {

// Optional setup/teardown interfaces for fixtures.
// Implement these in your fixture type if you need explicit hooks that may fail
// (avoid throwing from destructors).
struct FixtureSetup {
    virtual ~FixtureSetup() = default;
    virtual void setUp()    = 0;
};

struct FixtureTearDown {
    virtual ~FixtureTearDown() = default;
    virtual void tearDown()    = 0;
};

} // namespace gentest

