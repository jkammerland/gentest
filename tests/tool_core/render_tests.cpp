#include "render.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using gentest::codegen::FixtureDeclInfo;
using gentest::codegen::FixtureLifetime;
using gentest::codegen::FixtureScope;
using gentest::codegen::FreeCallArg;
using gentest::codegen::FreeCallArgKind;
using gentest::codegen::FreeFixtureUse;
using gentest::codegen::TestCaseInfo;
using gentest::codegen::render::TraitArrays;
using gentest::codegen::render::WrapperTemplates;

namespace {

struct Run {
    int  failures = 0;
    void expect(bool ok, std::string_view msg) {
        if (!ok) {
            ++failures;
            std::cerr << "FAIL: " << msg << "\n";
        }
    }
    void contains(std::string_view haystack, std::string_view needle, std::string_view msg) {
        expect(haystack.find(needle) != std::string_view::npos, msg);
    }
    void excludes(std::string_view haystack, std::string_view needle, std::string_view msg) {
        expect(haystack.find(needle) == std::string_view::npos, msg);
    }
};

} // namespace

int main() {
    using namespace gentest::codegen::render;

    Run t;

    {
        const std::filesystem::path fixture_path = std::filesystem::current_path() / "gentest_core_render_fixture.txt";
        {
            std::ofstream out(fixture_path, std::ios::binary);
            out << "alpha\nbeta";
        }
        t.expect(read_template_file(fixture_path) == "alpha\nbeta", "read_template_file reads file contents");
        t.expect(read_template_file(fixture_path.string() + ".missing").empty(), "read_template_file returns empty for missing file");

        std::error_code ec;
        std::filesystem::remove(fixture_path, ec);
    }

    t.expect(escape_string("\\\"\n\r\t") == R"(\\\"\n\r\t)", "escape_string escapes control characters");
    t.expect(render_forward_decls({}, "ignored", "ignored").empty(), "render_forward_decls stays empty");

    {
        std::vector<TestCaseInfo> cases(2);
        cases[1].tags         = {"fast", "owner=qa"};
        cases[1].requirements = {"REQ-7"};

        const TraitArrays arrays = render_trait_arrays(cases, "empty:{name}", "name={name};count={count};body={body}");
        t.expect(arrays.tag_names.size() == 2, "render_trait_arrays creates tag names");
        t.expect(arrays.req_names.size() == 2, "render_trait_arrays creates requirement names");
        t.expect(arrays.tag_names[0] == "{}", "render_trait_arrays uses empty tag span expression");
        t.expect(arrays.req_names[0] == "{}", "render_trait_arrays uses empty requirement span expression");
        t.expect(arrays.tag_names[1] == "{kTags_1, 2}", "render_trait_arrays uses non-empty tag range expression");
        t.expect(arrays.req_names[1] == "{kReqs_1, 1}", "render_trait_arrays uses non-empty requirement range expression");
        t.excludes(arrays.declarations, "empty:kTags_0", "render_trait_arrays skips empty tag array declarations");
        t.excludes(arrays.declarations, "empty:kReqs_0", "render_trait_arrays skips empty requirement array declarations");
        t.contains(arrays.declarations, "name=kTags_1;count=2;body=", "render_trait_arrays emits non-empty tag array");
        t.contains(arrays.declarations, "    \"fast\",\n", "render_trait_arrays renders first tag");
        t.contains(arrays.declarations, "    \"owner=qa\",\n", "render_trait_arrays renders second tag");
        t.contains(arrays.declarations, "name=kReqs_1;count=1;body=", "render_trait_arrays emits non-empty requirement array");
        t.contains(arrays.declarations, "    \"REQ-7\",\n", "render_trait_arrays renders requirement");
    }

    {
        std::vector<TestCaseInfo> cases(2);
        cases[0].display_name   = "suite/plain";
        cases[0].filename       = "plain.cpp";
        cases[0].line           = 17;
        cases[0].qualified_name = "suite::plain";
        cases[0].suite_name     = "suite";

        cases[1].display_name           = "bench/case";
        cases[1].filename               = "bench.cpp";
        cases[1].line                   = 23;
        cases[1].is_benchmark           = true;
        cases[1].is_jitter              = true;
        cases[1].is_baseline            = true;
        cases[1].should_skip            = true;
        cases[1].skip_reason            = "why \"quoted\"";
        cases[1].fixture_qualified_name = "fixtures::Shared";
        cases[1].fixture_lifetime       = FixtureLifetime::MemberSuite;
        cases[1].suite_name             = "bench/suite";

        const std::string rendered = render_case_entries(
            cases, {"kTags_0", "kTags_1"}, {"kReqs_0", "kReqs_1"},
            "N={name}|W={wrapper}|FN={fn}|SIMPLE={simple_fn}|F={file}|L={line}|B={is_bench}|J={is_jitter}|BASE={is_baseline}|T={tags}|"
            "R={reqs}|SK={skip_reason}"
            "|SS={should_skip}|FL={flags}|FX={fixture}|LT={lifetime}|SU={suite}\n");
        t.contains(rendered,
                   "N=suite/plain|W=::kCaseInvoke_0|FN=nullptr|SIMPLE=&::suite::plain|F=plain.cpp|L=17|B=false|J=false|BASE=false",
                   "render_case_entries renders plain case");
        t.contains(rendered, "N=bench/case|W=::kCaseInvoke_1|FN=&::kCaseInvoke_1|SIMPLE=nullptr|F=bench.cpp|L=23|B=true|J=true|BASE=true",
                   "render_case_entries renders wrapper-backed measured case");
        t.contains(rendered, "SK=std::string_view{}|SS=false|FL=0u|FX=std::string_view{}|LT=gentest::FixtureLifetime::None|SU=\"suite\"",
                   "render_case_entries renders empty skip and fixture fields");
        t.contains(rendered,
                   R"(SK="why \"quoted\""|SS=true|FL=15u|FX="fixtures::Shared"|LT=gentest::FixtureLifetime::MemberSuite|SU="bench/suite")",
                   "render_case_entries escapes skip reason and fixture name");
    }

    {
        std::vector<FixtureDeclInfo> fixtures;
        fixtures.push_back(FixtureDeclInfo{
            .qualified_name = "demo::LocalFx",
            .scope          = FixtureScope::Local,
        });
        fixtures.push_back(FixtureDeclInfo{
            .qualified_name = "demo::SuiteFx",
            .suite_name     = "suite/path",
            .scope          = FixtureScope::Suite,
        });
        fixtures.push_back(FixtureDeclInfo{
            .qualified_name = "::demo::GlobalFx",
            .scope          = FixtureScope::Global,
        });

        const std::string rendered = render_fixture_registrations(fixtures);
        t.excludes(rendered, "demo::LocalFx", "render_fixture_registrations skips local fixtures");
        t.contains(rendered,
                   "::gentest::detail::register_shared_fixture<::demo::SuiteFx>(::gentest::detail::SharedFixtureScope::Suite, "
                   "\"suite/path\", \"demo::SuiteFx\");",
                   "render_fixture_registrations prefixes suite fixture types");
        t.contains(rendered,
                   "::gentest::detail::register_shared_fixture<::demo::GlobalFx>(::gentest::detail::SharedFixtureScope::Global, "
                   "std::string_view{}, \"::demo::GlobalFx\");",
                   "render_fixture_registrations preserves leading global qualifiers");
    }

    {
        const std::string free_test_tpl     = "FREE_TEST {w}\n{invoke}\n";
        const std::string free_tpl          = "FREE {w}\n{invoke}\n";
        const std::string free_fixtures_tpl = "FREE_FIX {w}\n{decls}{inits}{setup_flags}{setup}{teardown}{invoke}\nBENCH\n{bench_decls}"
                                              "{bench_setup_flags}{bench_inits}{bench_setup}{bench_teardown}{bench_invoke}\n";
        const std::string ephemeral_tpl     = "EPHEMERAL {w}\n{fixture}\n{invoke}\n{bench_invoke}\n";
        const std::string stateful_tpl      = "STATEFUL {w}\n{fixture}\n{invoke}\n";
        const WrapperTemplates templates{
            .free_test     = free_test_tpl,
            .free          = free_tpl,
            .free_fixtures = free_fixtures_tpl,
            .ephemeral     = ephemeral_tpl,
            .stateful      = stateful_tpl,
        };

        std::vector<TestCaseInfo> cases;

        TestCaseInfo free_case;
        free_case.qualified_name  = "alpha::beta::plain_free";
        free_case.namespace_parts = {"alpha", "beta"};
        cases.push_back(free_case);

        TestCaseInfo value_case;
        value_case.qualified_name = "math::sum";
        value_case.call_arguments = "1, 2";
        value_case.returns_value  = true;
        cases.push_back(value_case);

        TestCaseInfo free_fixture_case;
        free_fixture_case.qualified_name = "fx::invoke";
        free_fixture_case.returns_value  = true;
        free_fixture_case.free_fixtures  = {
            FreeFixtureUse{.type_name = "fx::LocalFx", .scope = FixtureScope::Local},
            FreeFixtureUse{.type_name = "fx::SuiteFx", .scope = FixtureScope::Suite, .suite_name = "suite/path"},
            FreeFixtureUse{.type_name = "fx::GlobalFx", .scope = FixtureScope::Global},
        };
        free_fixture_case.free_call_args = {
            FreeCallArg{.kind = FreeCallArgKind::Fixture, .fixture_index = 0},
            FreeCallArg{.kind = FreeCallArgKind::Value, .value_expression = "7"},
            FreeCallArg{.kind = FreeCallArgKind::Fixture, .fixture_index = 1},
            FreeCallArg{.kind = FreeCallArgKind::Fixture, .fixture_index = 2},
        };
        cases.push_back(free_fixture_case);

        TestCaseInfo member_ephemeral;
        member_ephemeral.qualified_name         = "fixture::EphemeralFx::run";
        member_ephemeral.fixture_qualified_name = "fixture::EphemeralFx";
        member_ephemeral.fixture_lifetime       = FixtureLifetime::MemberEphemeral;
        cases.push_back(member_ephemeral);

        TestCaseInfo member_shared;
        member_shared.qualified_name         = "fixture::SharedFx::run";
        member_shared.fixture_qualified_name = "fixture::SharedFx";
        member_shared.fixture_lifetime       = FixtureLifetime::MemberSuite;
        member_shared.returns_value          = true;
        cases.push_back(member_shared);

        TestCaseInfo member_ephemeral_with_fixtures;
        member_ephemeral_with_fixtures.qualified_name         = "fixture::TemplatedFx::go";
        member_ephemeral_with_fixtures.fixture_qualified_name = "fixture::TemplatedFx";
        member_ephemeral_with_fixtures.fixture_lifetime       = FixtureLifetime::MemberEphemeral;
        member_ephemeral_with_fixtures.is_function_template   = true;
        member_ephemeral_with_fixtures.returns_value          = true;
        member_ephemeral_with_fixtures.free_fixtures  = {FreeFixtureUse{.type_name = "extras::LocalFx", .scope = FixtureScope::Local}};
        member_ephemeral_with_fixtures.free_call_args = {
            FreeCallArg{.kind = FreeCallArgKind::Fixture, .fixture_index = 0},
            FreeCallArg{.kind = FreeCallArgKind::Value, .value_expression = "42"},
        };
        cases.push_back(member_ephemeral_with_fixtures);

        TestCaseInfo member_shared_with_fixtures;
        member_shared_with_fixtures.qualified_name         = "fixture::SharedWithFx::go";
        member_shared_with_fixtures.fixture_qualified_name = "fixture::SharedWithFx";
        member_shared_with_fixtures.fixture_lifetime       = FixtureLifetime::MemberGlobal;
        member_shared_with_fixtures.free_fixtures          = {
            FreeFixtureUse{.type_name = "extras::SuiteFx", .scope = FixtureScope::Suite, .suite_name = "shared/suite"}};
        member_shared_with_fixtures.free_call_args = {FreeCallArg{.kind = FreeCallArgKind::Fixture, .fixture_index = 0}};
        cases.push_back(member_shared_with_fixtures);

        const std::string rendered = render_wrappers(cases, templates);
        t.excludes(rendered, "FREE_TEST kCaseInvoke_0", "render_wrappers skips direct free test wrappers");
        t.excludes(rendered, "plain_free", "render_wrappers leaves direct free test calls to generated metadata");
        t.contains(rendered, "[[maybe_unused]] const auto _ = ::__gentest_lookup_helper_1();",
                   "render_wrappers preserves return values for parameterized free cases");
        t.contains(rendered, "return math::sum(1, 2);", "render_wrappers forwards plain call arguments inside helpers");
        t.contains(rendered, "if (!gentest_init_fixture(fx0_, \"fx::LocalFx\")) return;",
                   "render_wrappers initializes local free fixtures");
        t.contains(rendered,
                   "if (!gentest_init_shared_fixture(fx1_, ::gentest::detail::SharedFixtureScope::Suite, \"suite/path\", \"fx::SuiteFx\")) "
                   "return;",
                   "render_wrappers initializes suite free fixtures");
        t.contains(rendered,
                   "if (!gentest_init_shared_fixture(fx2_, ::gentest::detail::SharedFixtureScope::Global, std::string_view{}, "
                   "\"fx::GlobalFx\")) return;",
                   "render_wrappers initializes global free fixtures");
        t.contains(rendered,
                   "return fx::invoke(static_cast<decltype(fx0)&&>(fx0), 7, static_cast<decltype(fx1)&&>(fx1), "
                   "static_cast<decltype(fx2)&&>(fx2));",
                   "render_wrappers forwards fixture-bound helper arguments");
        t.contains(rendered, "EPHEMERAL kCaseInvoke_3\nfixture::EphemeralFx\nstatic_cast<void>(::__gentest_lookup_helper_3(fx_.ref()));",
                   "render_wrappers emits plain ephemeral member wrapper content");
        t.contains(rendered, "[[maybe_unused]] const auto _ = ::__gentest_lookup_helper_4(*fx_);",
                   "render_wrappers preserves return values for shared members");
        t.contains(rendered, "static_cast<decltype(self)&&>(self).template go(static_cast<decltype(fx0)&&>(fx0), 42)",
                   "render_wrappers emits templated member helper calls");
        t.contains(rendered, R"(gentest_record_fixture_failure("fixture::SharedWithFx", "instance missing");)",
                   "render_wrappers reports missing shared member fixtures");
        t.contains(rendered, "static thread_local BenchState bench_state{};", "render_wrappers emits measured bench state");
    }

    if (t.failures != 0) {
        std::cerr << "Total failures: " << t.failures << "\n";
        return 1;
    }
    return 0;
}
