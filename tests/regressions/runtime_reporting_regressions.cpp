#include "gentest/detail/registration_runtime.h"
#include "gentest/runner.h"

#include <span>
#include <string>
#include <string_view>

using namespace gentest::asserts;

namespace {

void fallback_assertion_failure(void *) { throw gentest::assertion("runtime-reporting-fallback-assertion"); }

void junit_cdata_close_token_failure(void *) { EXPECT_TRUE(false, "runtime-reporting-cdata-token ]]> marker"); }

void junit_invalid_xml_control_failure(void *) {
    std::string message = "runtime-reporting-xml-control ";
    message.push_back('\0');
    message.push_back('\x1B');
    message += " marker";
    EXPECT_TRUE(false, message);
}

void github_annotation_xpass_with_punctuation(void *) { gentest::xfail("runtime-reporting-annotation"); }

void pass_for_junit_io_visibility(void *) {}

constexpr char             kWindowsStyleFile[]       = "C:/repo,win/src/runtime_reporting_case.cpp";
constexpr char             kXmlControlRequirement[]  = {'R', 'E', 'Q', '-', '\x1B', ' ', 'm', 'a', 'r', 'k', 'e', 'r'};
constexpr std::string_view kXmlControlRequirements[] = {std::string_view{kXmlControlRequirement, sizeof(kXmlControlRequirement)}};

gentest::Case kCases[] = {
    {
        .name             = "regressions/runtime_reporting/fallback_assertion_failure",
        .fn               = &fallback_assertion_failure,
        .file             = __FILE__,
        .line             = __LINE__,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    },
    {
        .name             = "regressions/runtime_reporting/junit_cdata_close_token_failure",
        .fn               = &junit_cdata_close_token_failure,
        .file             = __FILE__,
        .line             = __LINE__,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    },
    {
        .name             = "regressions/runtime_reporting/junit_invalid_xml_control_failure",
        .fn               = &junit_invalid_xml_control_failure,
        .file             = __FILE__,
        .line             = __LINE__,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = kXmlControlRequirements,
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    },
    {
        .name             = "regressions/runtime_reporting/gha,title:punct",
        .fn               = &github_annotation_xpass_with_punctuation,
        .file             = kWindowsStyleFile,
        .line             = 77,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    },
    {
        .name             = "regressions/runtime_reporting/pass_for_junit_io_visibility",
        .fn               = &pass_for_junit_io_visibility,
        .file             = __FILE__,
        .line             = __LINE__,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    },
};

} // namespace

int main(int argc, char **argv) {
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
