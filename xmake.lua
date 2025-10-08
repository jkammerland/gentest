set_project("gentest")
set_languages("c++23")

add_rules("mode.debug", "mode.release")

-- Common compile options
add_cxxflags("-Wno-unknown-attributes", "-Wno-attributes", {public = true})

-- Header-only consumer interface
gentest_includes = {"include"}

-- Code generator
target("gentest_codegen")
    set_kind("binary")
    add_defines("FMT_HEADER_ONLY", string.format('GENTEST_VERSION_STR="%s"', "1.0.0"))
    add_includedirs(gentest_includes)
    add_ldflags("-L/usr/local/lib", "-Wl,-rpath,/usr/local/lib")
    add_links("clang-cpp")
    add_files(
        "tools/src/main.cpp",
        "tools/src/parse_core.cpp",
        "tools/src/parse.cpp",
        "tools/src/discovery.cpp",
        "tools/src/mock_discovery.cpp",
        "tools/src/validate.cpp",
        "tools/src/emit.cpp",
        "tools/src/render_mocks.cpp",
        "tools/src/type_kind.cpp",
        "tools/src/render.cpp",
        "tools/src/tooling_support.cpp"
    )

-- Phony generator + test target: one-step per suite
function gentest_suite(suite)
    local gendir_rel = path.join("$(builddir)", "gen", suite)

    target("gentest_gen_" .. suite)
        set_kind("phony")
        add_deps("gentest_codegen")
        on_build(function (t)
            local projectdir = os.projectdir()
            local builddir = "build"
            local outdir = path.join(projectdir, builddir, "gen", suite)
            os.mkdir(outdir)
            local out_cpp = path.join(outdir, "cases_test_impl.cpp")
            local reg = path.join(outdir, "cases_mock_registry.hpp")
            local impl = path.join(outdir, "cases_mock_impl.hpp")
            local cases = path.join(projectdir, "tests", suite, "cases.cpp")
            -- Locate built codegen in build tree, fallback to PATH
            local found = os.files(path.join(projectdir, builddir, "**", "gentest_codegen"))
            local codegen = (found and #found > 0) and found[1] or "gentest_codegen"
            os.runv(codegen, {"--output", out_cpp,
                               "--entry", "gentest::run_all_tests",
                               "--mock-registry", reg,
                               "--mock-impl", impl,
                               cases, "--",
                               "-std=c++23",
                               "-I" .. path.join(projectdir, "include"),
                               "-I" .. path.join(projectdir, "tests")})
        end)

    target("gentest_" .. suite .. "_tests")
        set_kind("binary")
        add_deps("gentest_gen_" .. suite)
        add_includedirs(gentest_includes, "tests", gendir_rel)
        add_defines("FMT_HEADER_ONLY",
                    "GENTEST_MOCK_REGISTRY_PATH=cases_mock_registry.hpp",
                    "GENTEST_MOCK_IMPL_PATH=cases_mock_impl.hpp")
        add_files("tests/support/test_entry.cpp",
                  path.join(gendir_rel, "cases_test_impl.cpp"))
end

-- Core suites
gentest_suite("unit")
gentest_suite("integration")
gentest_suite("skiponly")
gentest_suite("fixtures")
-- Optional
gentest_suite("templates")
gentest_suite("mocking")
