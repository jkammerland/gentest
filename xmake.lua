set_project("gentest")
set_languages("cxx20")
set_policy("build.ccache", false)

add_rules("mode.debug", "mode.release")
add_requires("fmt")

local project_root = os.scriptdir()
local codegen_project_root = project_root
local xmake_file = path.join(project_root, "xmake.lua")
if os.islink and os.readlink then
    local ok, resolved_xmake = pcall(os.readlink, xmake_file)
    if ok and resolved_xmake and resolved_xmake ~= "" then
        codegen_project_root = path.directory(resolved_xmake)
    end
end
local incdirs = {"include", "tests", "third_party/include"}
local gentest_common_defines = {"FMT_HEADER_ONLY"}
-- The validated checked-in Xmake path uses Clang, including on Windows.
-- Keep the common warning suppression in Clang/GNU form here instead of
-- forcing MSVC-only /wd flags into clang++ builds.
local gentest_common_cxxflags = {"-Wno-attributes"}

local function current_gen_root()
    local buildir = get_config("builddir") or get_config("buildir") or "build"
    if is_host("windows") then
        local plat = get_config("plat") or os.host()
        local arch = get_config("arch") or os.arch()
        local mode = get_config("mode") or "release"
        return path.join(buildir, "g", plat, arch, mode == "debug" and "d" or "r")
    end
    local plat = get_config("plat") or os.host()
    local arch = get_config("arch") or os.arch()
    local mode = get_config("mode") or "release"
    return path.join(buildir, "gen", plat, arch, mode)
end

local enable_module_targets = os.getenv("GENTEST_XMAKE_SKIP_MODULE_TARGETS") ~= "1"

includes("xmake/gentest.lua")
gentest_configure({
    project_root = project_root,
    codegen_project_root = codegen_project_root,
    incdirs = incdirs,
    gentest_common_defines = gentest_common_defines,
    gentest_common_cxxflags = gentest_common_cxxflags,
})

target("gentest_runtime")
    set_kind("static")
    gentest_apply_windows_llvm_toolchain()
    add_packages("fmt")
    add_files("src/bench_stats.cpp")
    add_files("src/context_api.cpp")
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
    add_files("src/runner_reporting_allure.cpp")
    add_files("src/runner_selector.cpp")
    add_files("src/runner_test_executor.cpp")
    add_files("src/runner_test_plan.cpp")
    add_includedirs(incdirs)
    add_defines(gentest_common_defines)
    add_cxxflags(table.unpack(gentest_common_cxxflags), {force = true})

if enable_module_targets then
    target("gentest")
        set_kind("static")
        gentest_apply_windows_llvm_toolchain()
        add_packages("fmt")
        add_includedirs(incdirs, {public = true})
        add_defines(gentest_common_defines)
        add_cxxflags(table.unpack(gentest_common_cxxflags), {force = true})
        add_files("include/gentest/gentest.cppm", {public = true})
        add_files("include/gentest/gentest.mock.cppm", {public = true})
        add_files("include/gentest/gentest.bench_util.cppm", {public = true})
        add_deps("gentest_runtime", {public = true})
end

target("gentest_main")
    set_kind("static")
    gentest_apply_windows_llvm_toolchain()
    add_packages("fmt")
    add_files("src/gentest_main.cpp")
    add_includedirs(incdirs)
    add_defines(gentest_common_defines)
    add_cxxflags(table.unpack(gentest_common_cxxflags), {force = true})
    add_deps("gentest_runtime", {public = true})

local function gentest_suite(name)
    target("gentest_" .. name .. "_xmake")
        set_kind("binary")
        gentest_apply_windows_llvm_toolchain()
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

target("gentest_consumer_textual_mocks_xmake")
    set_kind("static")
    gentest_apply_windows_llvm_toolchain()
    gentest_add_mocks({
        name = "gentest_consumer_textual_mocks_xmake",
        kind = "textual",
        defs = {"tests/consumer/header_mock_defs.hpp"},
        headerfiles = {"tests/consumer/header_mock_defs.hpp", "tests/consumer/service.hpp"},
        header_name = "gentest_consumer_mocks.hpp",
        output_dir = path.join(current_gen_root(), "consumer_textual_mocks"),
        deps = {"gentest_runtime"},
        target_id = "consumer_textual_mocks",
        defines = {"GENTEST_XMAKE_TEXTUAL_MOCKS_DEFINE=1"},
        clang_args = {"-DGENTEST_XMAKE_TEXTUAL_MOCKS_CODEGEN=1"},
    })

target("gentest_consumer_textual_xmake")
    set_kind("binary")
    gentest_apply_windows_llvm_toolchain()
    gentest_attach_codegen({
        name = "gentest_consumer_textual_xmake",
        kind = "textual",
        source = "tests/consumer/cases.cpp",
        main = "tests/consumer/main.cpp",
        output_dir = path.join(current_gen_root(), "consumer_textual"),
        deps = {"gentest_main", "gentest_consumer_textual_mocks_xmake"},
        defines = {"GENTEST_XMAKE_TEXTUAL_CONSUMER_DEFINE=1"},
        clang_args = {"-DGENTEST_XMAKE_TEXTUAL_CONSUMER_CODEGEN=1"},
    })

if enable_module_targets then
    target("gentest_consumer_module_mocks_xmake")
        set_kind("static")
        gentest_apply_windows_llvm_toolchain()
        gentest_add_mocks({
            name = "gentest_consumer_module_mocks_xmake",
            kind = "modules",
            defs = {"tests/consumer/service_module.cppm", "tests/consumer/module_mock_defs.cppm"},
            defs_modules = {"gentest.consumer_service", "gentest.consumer_mock_defs"},
            headerfiles = {"tests/consumer/service_module.cppm", "tests/consumer/module_mock_defs.cppm"},
            module_name = "gentest.consumer_mocks",
            output_dir = path.join(current_gen_root(), "consumer_module_mocks"),
            deps = {"gentest"},
            target_id = "consumer_module_mocks",
            defines = {"GENTEST_XMAKE_MODULE_MOCKS_DEFINE=1"},
            clang_args = {"-DGENTEST_XMAKE_MODULE_MOCKS_CODEGEN=1"},
        })

    target("gentest_consumer_module_xmake")
        set_kind("binary")
        gentest_apply_windows_llvm_toolchain()
        gentest_attach_codegen({
            name = "gentest_consumer_module_xmake",
            kind = "modules",
            module_name = "gentest.consumer_cases",
            source = "tests/consumer/cases.cppm",
            main = "tests/consumer/main.cpp",
            output_dir = path.join(current_gen_root(), "consumer_module"),
            deps = {"gentest_main", "gentest", "gentest_consumer_module_mocks_xmake"},
            defines = {"GENTEST_CONSUMER_USE_MODULES=1", "GENTEST_XMAKE_MODULE_CONSUMER_DEFINE=1"},
            clang_args = {"-DGENTEST_XMAKE_MODULE_CONSUMER_CODEGEN=1"},
        })
end

target("poc_cross_aarch64_qemu")
    set_kind("phony")
    on_run(function ()
        -- Use a plain shell call; this target is marked local/manual in other build systems too.
        os.vrunv("bash", {path.join(project_root, "scripts", "poc_cross_aarch64_qemu.sh")})
    end)

target("poc_cross_riscv64_qemu")
    set_kind("phony")
    on_run(function ()
        -- Use a plain shell call; this target is marked local/manual in other build systems too.
        os.vrunv("bash", {path.join(project_root, "scripts", "poc_cross_riscv64_qemu.sh")})
    end)
