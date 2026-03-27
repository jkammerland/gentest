set_project("gentest")
set_languages("cxx20")

add_rules("mode.debug", "mode.release")
add_requires("fmt")

local project_root = os.scriptdir()
local incdirs = {"include", "tests", "third_party/include"}
local buildsystem_codegen = path.join(project_root, "scripts", "gentest_buildsystem_codegen.py")
local python_program = is_host("windows") and "python" or "python3"

local gentest_common_defines = {"FMT_HEADER_ONLY"}
local gentest_common_cxxflags = {}
if is_plat("windows") then
    gentest_common_cxxflags = {"/wd5030"}
else
    gentest_common_cxxflags = {"-Wno-attributes"}
end

local function current_gen_root()
    local buildir = get_config("builddir") or get_config("buildir") or "build"
    local plat = get_config("plat") or os.host()
    local arch = get_config("arch") or os.arch()
    local mode = get_config("mode") or "release"
    return path.join(buildir, "gen", plat, arch, mode)
end

includes("xmake/gentest.lua")
gentest_configure({
    project_root = project_root,
    codegen_project_root = project_root,
    incdirs = incdirs,
    buildsystem_codegen = buildsystem_codegen,
    python_program = python_program,
    gentest_common_defines = gentest_common_defines,
    gentest_common_cxxflags = gentest_common_cxxflags,
})

target("gentest_runtime")
    set_kind("static")
    add_packages("fmt")
    add_files("src/bench_stats.cpp")
    add_files("src/runtime_context.cpp")
    add_files("src/runner_case_invoker.cpp")
    add_files("src/runner_cli.cpp")
    add_files("src/runner_fixture_runtime.cpp")
    add_files("src/runner_measured_executor.cpp")
    add_files("src/runner_measured_format.cpp")
    add_files("src/runner_measured_report.cpp")
    add_files("src/runner_impl.cpp")
    add_files("src/runner_orchestrator.cpp")
    add_files("src/runner_reporting.cpp")
    add_files("src/runner_selector.cpp")
    add_files("src/runner_test_executor.cpp")
    add_files("src/runner_test_plan.cpp")
    add_includedirs(incdirs)
    add_defines(gentest_common_defines)
    add_cxxflags(table.unpack(gentest_common_cxxflags), {force = true})

target("gentest")
    set_kind("static")
    add_packages("fmt")
    add_includedirs(incdirs, {public = true})
    add_defines(gentest_common_defines)
    add_cxxflags(table.unpack(gentest_common_cxxflags), {force = true})
    add_files("include/gentest/gentest.cppm", {public = true})
    add_files("include/gentest/gentest.mock.cppm", {public = true})
    add_files("include/gentest/gentest.bench_util.cppm", {public = true})
    add_deps("gentest_runtime", {public = true})

target("gentest_main")
    set_kind("static")
    add_packages("fmt")
    add_files("src/gentest_main.cpp")
    add_includedirs(incdirs)
    add_defines(gentest_common_defines)
    add_cxxflags(table.unpack(gentest_common_cxxflags), {force = true})
    add_deps("gentest", {public = true})

local function gentest_suite(name)
    gentest_attach_codegen({
        name = "gentest_" .. name .. "_xmake",
        kind = "textual",
        source = path.join("tests", name, "cases.cpp"),
        output_dir = path.join(current_gen_root(), name),
        deps = {"gentest_main"},
    })
end

gentest_suite("unit")
gentest_suite("integration")
gentest_suite("fixtures")
gentest_suite("skiponly")

local consumer_textual_mocks = gentest_add_mocks({
    name = "gentest_consumer_textual_mocks_xmake",
    kind = "textual",
    defs = {"tests/consumer/header_mock_defs.hpp"},
    headerfiles = {"tests/consumer/header_mock_defs.hpp", "tests/consumer/service.hpp"},
    header_name = "gentest_consumer_mocks.hpp",
    output_dir = path.join(current_gen_root(), "consumer_textual_mocks"),
    deps = {"gentest_runtime"},
    target_id = "consumer_textual_mocks",
})

gentest_attach_codegen({
    name = "gentest_consumer_textual_xmake",
    kind = "textual",
    source = "tests/buildsystems/consumer_textual_cases.cpp",
    main = "tests/consumer/main.cpp",
    output_dir = path.join(current_gen_root(), "consumer_textual"),
    deps = {"gentest_main", consumer_textual_mocks},
})

local consumer_module_mocks = gentest_add_mocks({
    name = "gentest_consumer_module_mocks_xmake",
    kind = "modules",
    defs = {"tests/consumer/simple_module_mock_defs.cppm"},
    headerfiles = {"tests/consumer/simple_module_mock_defs.cppm"},
    module_name = "gentest.consumer_simple_mocks",
    output_dir = path.join(current_gen_root(), "consumer_module_mocks"),
    deps = {"gentest"},
    target_id = "consumer_module_mocks",
})

gentest_attach_codegen({
    name = "gentest_consumer_module_xmake",
    kind = "modules",
    source = "tests/buildsystems/consumer_simple_module_cases.cppm",
    output_dir = path.join(current_gen_root(), "consumer_module"),
    deps = {"gentest_main", consumer_module_mocks},
})

target("poc_cross_aarch64_qemu")
    set_kind("phony")
    on_run(function ()
        -- Use a plain shell call; this target is marked local/manual in other build systems too.
        os.vrunv("bash", {path.join(project_root, "scripts", "poc_cross_aarch64_qemu.sh")})
    end)
