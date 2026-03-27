set_project("gentest")
set_languages("cxx20")

add_rules("mode.debug", "mode.release")

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

-- Resolve gentest_codegen path.
-- Prefer a prebuilt binary via $GENTEST_CODEGEN; otherwise fall back to a CMake build dir.
local function resolve_codegen()
    local env_path = os.getenv("GENTEST_CODEGEN")
    if env_path and os.isfile(env_path) then
        local compdb_dir = path.directory(path.directory(env_path))
        if os.isfile(path.join(compdb_dir, "compile_commands.json")) then
            return env_path, compdb_dir, nil
        end
        return env_path, nil, nil
    end

    local build_dir = path.join(project_root, "build", "xmake-codegen", os.host(), os.arch())
    local bin = is_host("windows") and path.join(build_dir, "tools", "Release", "gentest_codegen.exe") or
                    path.join(build_dir, "tools", "gentest_codegen")
    local compdb_dir = nil
    if os.isfile(path.join(build_dir, "compile_commands.json")) then
        compdb_dir = build_dir
    end
    return bin, compdb_dir, build_dir
end

local function ensure_codegen(batchcmds)
    local codegen, compdb_dir, cmake_build_dir = resolve_codegen()
    if cmake_build_dir and not os.isfile(codegen) then
        batchcmds:vrunv("cmake", {"-S", project_root, "-B", cmake_build_dir, "-DCMAKE_BUILD_TYPE=Release",
                                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"})
        batchcmds:vrunv("cmake", {"--build", cmake_build_dir, "--target", "gentest_codegen", "-j", "1"})
        if is_host("windows") then
            if os.isfile(path.join(cmake_build_dir, "tools", "Release", "gentest_codegen.exe")) then
                codegen = path.join(cmake_build_dir, "tools", "Release", "gentest_codegen.exe")
            else
                codegen = path.join(cmake_build_dir, "tools", "gentest_codegen.exe")
            end
        else
            codegen = path.join(cmake_build_dir, "tools", "gentest_codegen")
        end
        compdb_dir = cmake_build_dir
    end
    return codegen, compdb_dir
end

local function current_gen_root()
    local buildir = get_config("builddir") or get_config("buildir") or "build"
    local plat = get_config("plat") or os.host()
    local arch = get_config("arch") or os.arch()
    local mode = get_config("mode") or "release"
    return path.join(buildir, "gen", plat, arch, mode)
end

local function project_path(filepath)
    if path.is_absolute(filepath) then
        return filepath
    end
    return path.join(project_root, filepath)
end

local function append_common_codegen_clang_args(args, extra_include_dirs)
    table.insert(args, "--clang-arg=-std=c++20")
    table.insert(args, "--clang-arg=-DGENTEST_CODEGEN=1")
    table.insert(args, "--clang-arg=-DFMT_HEADER_ONLY")
    table.insert(args, "--clang-arg=-Wno-unknown-attributes")
    table.insert(args, "--clang-arg=-Wno-attributes")
    table.insert(args, "--clang-arg=-Wno-unknown-warning-option")
    table.insert(args, "--clang-arg=-I" .. path.join(project_root, "include"))
    table.insert(args, "--clang-arg=-I" .. path.join(project_root, "tests"))
    table.insert(args, "--clang-arg=-I" .. path.join(project_root, "third_party", "include"))
    for _, include_dir in ipairs(extra_include_dirs or {}) do
        table.insert(args, "--clang-arg=-I" .. include_dir)
    end
end

local function run_textual_mock_codegen(batchcmds, codegen, compdb_dir, config)
    local args = {
        buildsystem_codegen,
        "--backend", "xmake",
        "--mode", "mocks",
        "--kind", "textual",
        "--codegen", codegen,
        "--source-root", project_root,
        "--out-dir", config.out_dir_abs,
        "--wrapper-output", config.wrapper_output,
        "--anchor-output", config.anchor_output,
        "--header-output", config.header_output,
        "--public-header", config.public_header,
        "--mock-registry", config.mock_registry,
        "--mock-impl", config.mock_impl,
        "--target-id", config.target_id,
        "--defs-file", config.defs_file,
        "--include-root", path.join(project_root, "include"),
        "--include-root", path.join(project_root, "tests"),
        "--include-root", path.join(project_root, "third_party", "include"),
    }
    if compdb_dir then
        table.insert(args, "--compdb")
        table.insert(args, compdb_dir)
    end
    append_common_codegen_clang_args(args)
    batchcmds:vrunv(python_program, args)
end

target("gentest_runtime")
    set_kind("static")
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

target("gentest_main")
    set_kind("static")
    add_files("src/gentest_main.cpp")
    add_includedirs(incdirs)
    add_defines(gentest_common_defines)
    add_cxxflags(table.unpack(gentest_common_cxxflags), {force = true})
    add_deps("gentest_runtime")

local function gentest_suite(name)
    local out_dir = path.join(current_gen_root(), name)
    local wrapper_cpp = path.join(out_dir, "tu_0000_cases.gentest.cpp")
    local wrapper_h = path.join(out_dir, "tu_0000_cases.gentest.h")
    local wrapper_d = path.join(out_dir, "tu_0000_cases.gentest.d")
    local source_file = path.join(project_root, "tests", name, "cases.cpp")

    target("gentest_" .. name .. "_xmake")
        set_kind("binary")
        add_includedirs(incdirs)
        add_defines(gentest_common_defines)
        add_cxxflags(table.unpack(gentest_common_cxxflags), {force = true})
        add_files(wrapper_cpp, {always_added = true})
        add_deps("gentest_main")
        before_buildcmd(function (target, batchcmds)
            local codegen, compdb_dir = ensure_codegen(batchcmds)

            local args = {
                buildsystem_codegen,
                "--backend", "xmake",
                "--mode", "suite",
                "--kind", "textual",
                "--codegen", codegen,
                "--source-root", project_root,
                "--out-dir", project_path(out_dir),
                "--wrapper-output", project_path(wrapper_cpp),
                "--header-output", project_path(wrapper_h),
                "--depfile", project_path(wrapper_d),
                "--source-file", source_file,
            }
            if compdb_dir then
                table.insert(args, "--compdb")
                table.insert(args, compdb_dir)
            end
            append_common_codegen_clang_args(args)
            batchcmds:vrunv(python_program, args)
        end)
end

gentest_suite("unit")
gentest_suite("integration")
gentest_suite("fixtures")
gentest_suite("skiponly")

local function gentest_textual_consumer()
    local gen_root = current_gen_root()
    local mock_out_dir = path.join(gen_root, "consumer_textual_mocks")
    local mock_out_dir_abs = project_path(mock_out_dir)
    local mock_defs_cpp = path.join(mock_out_dir, "consumer_textual_mocks_defs.cpp")
    local mock_anchor_cpp = path.join(mock_out_dir, "consumer_textual_mocks_anchor.cpp")
    local mock_codegen_h = path.join(mock_out_dir, "tu_0000_consumer_textual_mocks_defs.gentest.h")
    local mock_registry_h = path.join(mock_out_dir, "consumer_textual_mocks_mock_registry.hpp")
    local mock_impl_h = path.join(mock_out_dir, "consumer_textual_mocks_mock_impl.hpp")
    local mock_domain_registry_h = path.join(mock_out_dir, "consumer_textual_mocks_mock_registry__domain_0000_header.hpp")
    local mock_domain_impl_h = path.join(mock_out_dir, "consumer_textual_mocks_mock_impl__domain_0000_header.hpp")
    local mock_public_h = path.join(mock_out_dir, "gentest_consumer_mocks.hpp")
    local mock_defs_source = path.join(project_root, "tests", "consumer", "header_mock_defs.hpp")
    local mock_codegen_config = {
        out_dir_abs = mock_out_dir_abs,
        wrapper_output = project_path(mock_defs_cpp),
        anchor_output = project_path(mock_anchor_cpp),
        header_output = project_path(mock_codegen_h),
        public_header = project_path(mock_public_h),
        mock_registry = project_path(mock_registry_h),
        mock_impl = project_path(mock_impl_h),
        target_id = "consumer_textual_mocks",
        defs_file = mock_defs_source,
    }

    target("gentest_consumer_textual_mocks_xmake")
        set_kind("static")
        set_policy("build.fence", true)
        add_includedirs(incdirs)
        add_includedirs(mock_out_dir_abs, {public = true})
        add_defines(gentest_common_defines)
        add_cxxflags(table.unpack(gentest_common_cxxflags), {force = true})
        add_files(mock_defs_cpp, mock_anchor_cpp, {always_added = true})
        add_headerfiles("tests/consumer/header_mock_defs.hpp", "tests/consumer/service.hpp")
        add_deps("gentest_runtime")
        before_buildcmd(function (_, batchcmds)
            local codegen, compdb_dir = ensure_codegen(batchcmds)
            run_textual_mock_codegen(batchcmds, codegen, compdb_dir, mock_codegen_config)
        end)

    local consumer_out_dir = path.join(gen_root, "consumer_textual")
    local consumer_wrapper_cpp = path.join(consumer_out_dir, "tu_0000_consumer_textual_cases.gentest.cpp")
    local consumer_wrapper_h = path.join(consumer_out_dir, "tu_0000_consumer_textual_cases.gentest.h")
    local consumer_wrapper_d = path.join(consumer_out_dir, "tu_0000_consumer_textual_cases.gentest.d")
    local consumer_source = path.join(project_root, "tests", "buildsystems", "consumer_textual_cases.cpp")

    target("gentest_consumer_textual_xmake")
        set_kind("binary")
        add_includedirs(incdirs)
        add_includedirs(mock_out_dir_abs)
        add_defines(gentest_common_defines)
        add_cxxflags(table.unpack(gentest_common_cxxflags), {force = true})
        add_files(consumer_wrapper_cpp, "tests/consumer/main.cpp", {always_added = true})
        add_deps("gentest_main", "gentest_consumer_textual_mocks_xmake")
        before_buildcmd(function (_, batchcmds)
            local codegen, compdb_dir = ensure_codegen(batchcmds)

            local args = {
                buildsystem_codegen,
                "--backend", "xmake",
                "--mode", "suite",
                "--kind", "textual",
                "--codegen", codegen,
                "--source-root", project_root,
                "--out-dir", project_path(consumer_out_dir),
                "--wrapper-output", project_path(consumer_wrapper_cpp),
                "--header-output", project_path(consumer_wrapper_h),
                "--depfile", project_path(consumer_wrapper_d),
                "--source-file", consumer_source,
            }
            if compdb_dir then
                table.insert(args, "--compdb")
                table.insert(args, compdb_dir)
            end
            append_common_codegen_clang_args(args, {mock_out_dir_abs})
            batchcmds:vrunv(python_program, args)
        end)
end

gentest_textual_consumer()

target("poc_cross_aarch64_qemu")
    set_kind("phony")
    on_run(function ()
        -- Use a plain shell call; this target is marked local/manual in other build systems too.
        os.vrunv("bash", {path.join(project_root, "scripts", "poc_cross_aarch64_qemu.sh")})
    end)
