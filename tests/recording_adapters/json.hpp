#pragma once
#include "gentest/record_glaze.h"
#include "gentest/runner.h"

#include <stdexcept>
#include <vector>

struct Snapshot {
    std::string      device;
    std::vector<int> samples;
};
struct BadJson {
    bool throws;
};
inline int encoded = 0;
namespace glz {
template <> struct to<JSON, BadJson> {
    template <auto Opts> static void op(const BadJson &value, is_context auto &ctx, auto &&...) {
        ++encoded;
        if (value.throws)
            throw std::runtime_error("deliberate encoder exception");
        ctx.error                = glz::error_code::syntax_error;
        ctx.custom_error_message = "deliberate encoder error";
    }
};
} // namespace glz
namespace adapter {
struct Measured : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override { encoded = 0; }
    void tearDown() override { gentest::record_property("encoded", encoded); }
};
[[gentest::test]] inline void success() {
    Snapshot value{"simulator", {1, 2, 3}};
    gentest::record_json("snapshot", value, {.schema = "snapshot/v1"});
    value.device = "mutated";
    gentest::record_json("empty", std::vector<int>{});
}
[[gentest::test]] inline void errors() {
    gentest::record_json("before", 42);
    gentest::record_json("bad", BadJson{false});
    gentest::record_json("throw", BadJson{true});
    gentest::record_json("after", 43);
}
[[gentest::bench("timed")]] inline void timed(Measured &) { gentest::record_json("forbidden", BadJson{false}); }
} // namespace adapter
