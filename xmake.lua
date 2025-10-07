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
    add_includedirs(gentest_includes, {public = false})
    -- Try llvm-config for includes; ignore failure
    before_build(function (target)
        import("core.base.option")
        import("core.project.project")
        local ok, out = pcall(os.iorun, "llvm-config --includedir")
        if ok and out and #out > 0 then
            target:add("includedirs", (out:gsub("\n$", "")))
        end
    end)
    add_ldflags("-L/usr/local/lib", "-Wl,-rpath,/usr/local/lib")
    add_links("clang-cpp")
    add_packages("fmt", {public = false})
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

-- Helper to attach codegen to a test target
function gentest_attach_codegen(target, suite)
    import("core.project.project")
    import("core.base.option")
    import("core.project.rule")

    target:add("includedirs", {"tests"})
    target:add("defines", "FMT_HEADER_ONLY")

    target:before_build(function (t)
        local outdir = path.join(os.curdir(), "build", t:name() .. "_gen")
        os.mkdir(outdir)
        local gen = path.join(os.curdir(), "build", os.host(), os.arch(), "gentest_codegen")
        if not os.isfile(gen) then
            -- try default target dir
            gen = path.join(os.curdir(), "build", "gentest_codegen")
        end
        if not os.isfile(gen) then
            -- fallback to system path
            gen = "gentest_codegen"
        end
        local cases = path.join("tests", suite, "cases.cpp")
        local out_cpp = path.join(outdir, "cases_test_impl.cpp")
        local reg = path.join(outdir, "cases_mock_registry.hpp")
        local impl = path.join(outdir, "cases_mock_impl.hpp")
        os.runv(gen, {"--output", out_cpp, "--entry", "gentest::run_all_tests", "--mock-registry", reg, "--mock-impl", impl, cases, "--", "-std=c++23", "-Iinclude", "-Itests"})
        t:add("files", out_cpp)
        t:add("includedirs", outdir)
        t:add("defines", "GENTEST_MOCK_REGISTRY_PATH=cases_mock_registry.hpp")
        t:add("defines", "GENTEST_MOCK_IMPL_PATH=cases_mock_impl.hpp")
    end)
end

function gentest_tests(name, suite)
    target(name)
        set_kind("binary")
        add_includedirs(gentest_includes)
        add_files("tests/support/test_entry.cpp")
        add_packages("fmt")
        gentest_attach_codegen(target, suite)
end

-- Passing suites
gentest_tests("gentest_unit_tests", "unit")
gentest_tests("gentest_integration_tests", "integration")
gentest_tests("gentest_skiponly_tests", "skiponly")
gentest_tests("gentest_fixtures_tests", "fixtures")
gentest_tests("gentest_templates_tests", "templates")
gentest_tests("gentest_mocking_tests", "mocking")

