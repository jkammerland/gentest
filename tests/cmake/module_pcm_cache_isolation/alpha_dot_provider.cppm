module;

#include <pcm_cache_shadow.hpp>

export module gentest.pcm_cache.alpha.beta.provider;

export namespace dot_provider {

#ifndef PCM_BUILD_ROOT
#define PCM_BUILD_ROOT "pcm_cache/default"
#endif

inline constexpr int         kValue     = 11;
inline constexpr const char *kBuildRoot = PCM_BUILD_ROOT;

[[using gentest: test(PCM_BUILD_ROOT)]]
void build_root_value() {}

} // namespace dot_provider
