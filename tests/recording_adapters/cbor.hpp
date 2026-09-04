#pragma once
#include "gentest/record_cbor.h"
#include "gentest/runner.h"

#include <cbor_tags/cbor_decoder.h>
#include <vector>

struct Snapshot {
    std::string      device;
    std::vector<int> samples;
    bool             operator==(const Snapshot &) const = default;
};
inline int encoded = 0;
struct BadCbor {
    template <class Encoder> auto encode(Encoder &) const -> typename Encoder::expected_type {
        ++encoded;
        return cbor::tags::unexpected<cbor::tags::status_code>(cbor::tags::status_code::error);
    }
};
struct Tagged {
    std::vector<std::byte>        bytes;
    template <class Encoder> auto encode(Encoder &encoder) const { return encoder(cbor::tags::static_tag<60000>{}, bytes); }
};
namespace adapter {
struct Measured : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override { encoded = 0; }
    void tearDown() override { gentest::record_property("encoded", encoded); }
};
[[gentest::test]] inline void success() {
    Snapshot value{"simulator", {1, 2, 3}};
    gentest::record_cbor("snapshot", value, {.schema = "snapshot/v1"});
    std::vector<std::byte> bytes;
    auto                   encoder = cbor::tags::make_encoder(bytes);
    gentest::require(encoder(value).has_value());
    Snapshot decoded;
    auto     decoder = cbor::tags::make_decoder(bytes);
    gentest::require(decoder(decoded).has_value());
    gentest::expect(decoded == value);
    value.device = "mutated";
    gentest::record_cbor("empty", std::vector<int>{});
    gentest::record_cbor("tagged", Tagged{{std::byte{0}, std::byte{255}}});
}
[[gentest::test]] inline void errors() {
    gentest::record_cbor("before", 42);
    gentest::record_cbor("bad", BadCbor{});
    gentest::record_cbor("after", 43);
}
[[gentest::bench("timed")]] inline void timed(Measured &) { gentest::record_cbor("forbidden", BadCbor{}); }
} // namespace adapter
