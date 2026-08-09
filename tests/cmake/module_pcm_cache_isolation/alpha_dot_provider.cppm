module;

#include <pcm_cache_shadow.hpp>

#if defined(PCM_CACHE_HEADER_BUILTIN_FILE)
#include <pcm_cache_builtin_file.hpp>
#elif defined(PCM_CACHE_HEADER_SOURCE_LOCATION)
#include <pcm_cache_source_location.hpp>
#endif

export module gentest.pcm_cache.alpha.beta.provider;

#ifdef PCM_CACHE_HEADER_FILE_MACRO
export {
#include "pcm_cache_file_macro.hpp"
}
#endif

export namespace dot_provider {

#ifndef PCM_BUILD_ROOT
#define PCM_BUILD_ROOT "pcm_cache/default"
#endif

inline constexpr int         kValue          = 11;
inline constexpr const char *kBuildRoot      = PCM_BUILD_ROOT;
inline constexpr const char *kSourceSpelling = __FILE__;
#if defined(PCM_CACHE_HEADER_BUILTIN_FILE) || defined(PCM_CACHE_HEADER_SOURCE_LOCATION)
inline constexpr const char *kHeaderLocationSpelling = ::gentest_pcm_header_location_spelling;
#endif

[[using gentest: test(PCM_BUILD_ROOT)]]
void build_root_value() {}

} // namespace dot_provider
