#pragma once

#include <source_location>

inline constexpr const char *gentest_pcm_header_location_spelling = std::source_location::current().file_name();
