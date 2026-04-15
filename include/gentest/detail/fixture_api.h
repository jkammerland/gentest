#pragma once

namespace gentest {

// Optional setup/teardown interfaces for fixtures.
//
// Implement these in your fixture type if you need explicit hooks that may
// fail (avoid throwing from destructors). The generated runner detects these
// interfaces at compile-time and invokes them around each local fixture use
// (ephemeral fixtures) or once for suite/global fixtures at the start/end of
// the test run.
struct FixtureSetup {
    virtual ~FixtureSetup() = default;
    virtual void setUp()    = 0;
};

struct FixtureTearDown {
    virtual ~FixtureTearDown() = default;
    virtual void tearDown()    = 0;
};

} // namespace gentest
