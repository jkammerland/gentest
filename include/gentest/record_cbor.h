#pragma once

#include "gentest/record.h"

#include <cbor_tags/cbor_encoder.h>
#include <exception>
#include <vector>

namespace gentest {

// Include explicitly and link cbor::tags on the consumer (C++20).
// For custom encoder options/extensions, encode externally and use record_data.
template <class T>
void record_cbor(std::string_view name, const T &value, RecordOptions options = {},
                 const std::source_location &loc = std::source_location::current()) {
    if (!detail::record_ready(name, "application/cbor", options, loc))
        return;
    std::vector<std::byte> bytes;
    try {
        auto       encoder = cbor::tags::make_encoder(bytes);
        const auto result  = encoder(value);
        if (!result) {
            detail::record_error("CBOR serialization failed (status " + std::to_string(static_cast<int>(result.error())) + ")", loc);
            return;
        }
    } catch (const std::exception &e) {
        detail::record_error(std::string("CBOR serialization threw: ") + e.what(), loc);
        return;
    } catch (...) {
        detail::record_error("CBOR serialization threw an unknown exception", loc);
        return;
    }
    record_data(name, bytes, "application/cbor", options, loc);
}

} // namespace gentest
