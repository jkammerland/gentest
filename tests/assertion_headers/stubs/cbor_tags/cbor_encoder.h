#pragma once

// Minimal stand-in for the cbor_tags encoder interface that
// gentest/record_cbor.h consumes. It lets the repository compile that adapter
// with exceptions disabled without requiring the optional dependency; the real
// library is exercised by tests/recording_adapters.

#include <cstddef>

namespace cbor::tags {

enum class status_code { error = 1 };

struct StubResult {
    bool ok;

    explicit    operator bool() const { return ok; }
    status_code error() const { return status_code::error; }
};

template <class Buffer> struct StubEncoder {
    Buffer *out;

    template <class T> StubResult operator()(const T &value) {
        out->push_back(static_cast<std::byte>(value));
        return {.ok = true};
    }
};

template <class Buffer> auto make_encoder(Buffer &buffer) { return StubEncoder<Buffer>{&buffer}; }

} // namespace cbor::tags
