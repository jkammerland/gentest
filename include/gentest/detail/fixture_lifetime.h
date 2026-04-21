#pragma once

namespace gentest {

enum class FixtureLifetime {
    None,
    MemberEphemeral,
    MemberSuite,
    MemberGlobal,
};

} // namespace gentest
