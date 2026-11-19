module;

#include <scale.hpp>

export module numbers.factor;

export constexpr int factor() { return BASE_FACTOR * scale + BASE_OFFSET; }
