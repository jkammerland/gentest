#pragma once

#include "gentest/runner.h"
#include "gentest/temporary_directory.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace temporary_directory {

namespace fs = std::filesystem;

inline void writeProbe(const gentest::TemporaryDirectory &directory) {
    gentest::assert_true(fs::is_directory(directory.path()));
#ifndef _WIN32
    constexpr auto other_permissions = fs::perms::group_all | fs::perms::others_all;
    gentest::assert_true((fs::status(directory.path()).permissions() & other_permissions) == fs::perms::none);
#endif

    std::ofstream probe(directory.path() / "probe.txt");
    probe << "fixture probe\n";
    probe.close();
    gentest::assert_true(probe.good());
}

[[gentest::test("pass")]] inline void passing(gentest::TemporaryDirectory &directory) {
    directory.keepOnFailure();
    writeProbe(directory);
}

[[gentest::test("skip")]] inline void skipping(gentest::TemporaryDirectory &directory) {
    directory.keepOnFailure();
    writeProbe(directory);
    gentest::skip("fixture skip");
}

[[gentest::test]] inline void cleanFailure(gentest::TemporaryDirectory &directory) {
    writeProbe(directory);
    throw std::runtime_error("fixture exception with cleanup");
}

[[gentest::test]] inline void keepException(gentest::TemporaryDirectory &directory) {
    writeProbe(directory);
    directory.keepOnFailure();
    throw std::runtime_error("fixture exception with retention");
}

[[gentest::test]] inline auto keepAssertion(gentest::TemporaryDirectory &directory) -> gentest::awaitable<> {
    writeProbe(directory);
    directory.keepOnFailure();
    co_await gentest::async::yield();
    gentest::assert_true(false, "fatal fixture assertion");
}

struct FailingSetup : gentest::FixtureSetup {
    void setUp() override { throw std::runtime_error("setup after directory creation failed"); }
};

[[gentest::test]] inline void cleanupAfterSetupFailure(gentest::TemporaryDirectory &, FailingSetup &) {
    gentest::fail("case body should never run");
}

struct FailingTeardown : gentest::FixtureTearDown {
    void tearDown() override { throw std::runtime_error("teardown after directory release failed"); }
};

[[gentest::test]] inline void keepTeardown(FailingTeardown &, gentest::TemporaryDirectory &directory) {
    writeProbe(directory);
    directory.keepOnFailure();
}

[[using gentest: bench("keepMeasurement"), bench_once]] inline void keepMeasurement(gentest::TemporaryDirectory &directory) {
    directory.keepOnFailure();
    gentest::expect(false, "measured call failed");
}

} // namespace temporary_directory
