#pragma once

namespace gentest {

// Optional setup/teardown interfaces for fixtures.
//
// Implement these in your fixture type if you need explicit hooks that may
// fail (avoid throwing from destructors). The generated runner detects these
// interfaces at compile-time and invokes them around each member test call
// (ephemeral fixtures) or around each call on the shared instance (stateful
// fixtures).
struct FixtureSetup {
    virtual ~FixtureSetup() = default;
    virtual void setUp()    = 0;
};

struct FixtureTearDown {
    virtual ~FixtureTearDown() = default;
    virtual void tearDown()    = 0;
};

} // namespace gentest
