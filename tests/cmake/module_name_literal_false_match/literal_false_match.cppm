#define BANNER    export module wrong.macro;
#define OFF_MACRO 0
#if 0
export module wrong.inactive;
#endif
#if defined(OFF_MACRO) && OFF_MACRO >= 2
export module wrong.conditional;
#endif
#if __has_include("definitely_missing_header.hpp")
export module wrong.has_include;
#endif
const char   *banner = "export module wrong.literal;";
export module real.module;
