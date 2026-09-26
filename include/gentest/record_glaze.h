#pragma once

#include "gentest/record.h"

#include <exception>
#include <glaze/json.hpp>

namespace gentest {

// Include explicitly and link glaze::glaze on the consumer (C++23).
// Glaze's normal metadata/custom writer specializations remain available.
template <class T>
void record_json(std::string_view name, const T &value, RecordOptions options = {},
                 const std::source_location &loc = std::source_location::current()) {
    if (!detail::record_ready(name, "application/json", options, loc))
        return;
    std::string bytes;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    try {
#endif
        const auto error = glz::write_json(value, bytes);
        if (error) {
            detail::record_error("JSON serialization failed: " + glz::format_error(error), loc);
            return;
        }
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    } catch (const std::exception &e) {
        detail::record_error(std::string("JSON serialization threw: ") + e.what(), loc);
        return;
    } catch (...) {
        detail::record_error("JSON serialization threw an unknown exception", loc);
        return;
    }
#endif
    record_data(name, std::as_bytes(std::span(bytes.data(), bytes.size())), "application/json", options, loc);
}

} // namespace gentest
