export module numbers.answer;

import numbers.factor;

#if defined(BASE_FACTOR) || defined(BASE_OFFSET)
#error "Private compile settings leaked from the base to the facade"
#endif

export constexpr int answer() { return factor() + 2; }
