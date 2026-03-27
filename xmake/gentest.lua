local gentest_state = {}

-- Configure the shared Xmake helper context. External consumers can override
-- codegen_project_root to point at a gentest checkout, or set GENTEST_CODEGEN
-- to use a prebuilt generator directly.
function gentest_configure(opts)
    gentest_state = opts or {}
end

local function state_value(key)
    local value = gentest_state[key]
    if value == nil then
        raise("gentest_configure must provide `" .. key .. "`")
    end
    return value
end

local function project_root()
    return state_value("project_root")
end

local function incdirs()
    return state_value("incdirs")
end

local function project_path(filepath)
    if path.is_absolute(filepath) then
        return filepath
    end
    return path.join(project_root(), filepath)
end

local function resolved_incdirs()
    local result = {}
    for _, include_dir in ipairs(incdirs()) do
        if path.is_absolute(include_dir) then
            table.insert(result, include_dir)
        else
            table.insert(result, project_path(include_dir))
        end
    end
    return result
end

local function codegen_project_root()
    return gentest_state["codegen_project_root"] or project_root()
end

local function buildsystem_codegen()
    return state_value("buildsystem_codegen")
end

local function python_program()
    return state_value("python_program")
end

local function gentest_common_defines()
    return state_value("gentest_common_defines")
end

local function gentest_common_cxxflags()
    return state_value("gentest_common_cxxflags")
end

local function append_common_codegen_clang_args(args, extra_include_dirs)
    table.insert(args, "--clang-arg=-std=c++20")
    table.insert(args, "--clang-arg=-DGENTEST_CODEGEN=1")
    table.insert(args, "--clang-arg=-DFMT_HEADER_ONLY")
    table.insert(args, "--clang-arg=-Wno-unknown-attributes")
    table.insert(args, "--clang-arg=-Wno-attributes")
    table.insert(args, "--clang-arg=-Wno-unknown-warning-option")
    for _, include_dir in ipairs(resolved_incdirs()) do
        table.insert(args, "--clang-arg=-I" .. include_dir)
    end
    for _, include_dir in ipairs(extra_include_dirs or {}) do
        table.insert(args, "--clang-arg=-I" .. include_dir)
    end
end

local function require_opt(opts, key, operation)
    local value = opts[key]
    if value == nil or value == "" then
        raise(operation .. " requires `" .. key .. "`")
    end
    return value
end

local function require_textual_kind(opts, operation)
    local kind = opts.kind or "textual"
    if kind ~= "textual" then
        raise(operation .. " only supports kind='textual' right now")
    end
    return kind
end

local function sanitize_target_id(name)
    return (name:gsub("[^%w_]", "_"))
end

local function resolve_codegen()
    local env_path = os.getenv("GENTEST_CODEGEN")
    if env_path and os.isfile(env_path) then
        local compdb_dir = path.directory(path.directory(env_path))
        if os.isfile(path.join(compdb_dir, "compile_commands.json")) then
            return env_path, compdb_dir, nil
        end
        return env_path, nil, nil
    end

    local build_dir = path.join(codegen_project_root(), "build", "xmake-codegen", os.host(), os.arch())
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
        batchcmds:vrunv("cmake", {"-S", codegen_project_root(), "-B", cmake_build_dir, "-DCMAKE_BUILD_TYPE=Release",
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

local function run_textual_mock_codegen(batchcmds, codegen, compdb_dir, config)
    local args = {
        buildsystem_codegen(),
        "--backend", "xmake",
        "--mode", "mocks",
        "--kind", "textual",
        "--codegen", codegen,
        "--source-root", project_root(),
        "--out-dir", config.out_dir_abs,
        "--wrapper-output", config.wrapper_output,
        "--anchor-output", config.anchor_output,
        "--header-output", config.header_output,
        "--public-header", config.public_header,
        "--mock-registry", config.mock_registry,
        "--mock-impl", config.mock_impl,
        "--target-id", config.target_id,
    }
    for _, include_dir in ipairs(resolved_incdirs()) do
        table.insert(args, "--include-root")
        table.insert(args, include_dir)
    end
    for _, defs_file in ipairs(config.defs) do
        table.insert(args, "--defs-file")
        table.insert(args, defs_file)
    end
    if compdb_dir then
        table.insert(args, "--compdb")
        table.insert(args, compdb_dir)
    end
    append_common_codegen_clang_args(args)
    batchcmds:vrunv(python_program(), args)
end

function gentest_add_mocks(opts)
    require_textual_kind(opts, "gentest_add_mocks")

    local target_name = require_opt(opts, "name", "gentest_add_mocks")
    local output_dir = require_opt(opts, "output_dir", "gentest_add_mocks")
    local public_header_name = require_opt(opts, "header_name", "gentest_add_mocks")
    local defs = require_opt(opts, "defs", "gentest_add_mocks")
    if type(defs) ~= "table" or #defs == 0 then
        raise("gentest_add_mocks requires `defs` to contain at least one file")
    end

    local target_id = opts.target_id or sanitize_target_id(target_name)
    local out_dir_abs = project_path(output_dir)
    local defs_cpp = path.join(output_dir, target_id .. "_defs.cpp")
    local anchor_cpp = path.join(output_dir, target_id .. "_anchor.cpp")
    local codegen_h = path.join(output_dir, "tu_0000_" .. target_id .. "_defs.gentest.h")
    local mock_registry_h = path.join(output_dir, target_id .. "_mock_registry.hpp")
    local mock_impl_h = path.join(output_dir, target_id .. "_mock_impl.hpp")
    local public_header = path.join(output_dir, public_header_name)
    local config = {
        defs = {},
        out_dir_abs = out_dir_abs,
        wrapper_output = project_path(defs_cpp),
        anchor_output = project_path(anchor_cpp),
        header_output = project_path(codegen_h),
        public_header = project_path(public_header),
        mock_registry = project_path(mock_registry_h),
        mock_impl = project_path(mock_impl_h),
        target_id = target_id,
    }
    for _, defs_file in ipairs(defs) do
        table.insert(config.defs, project_path(defs_file))
    end

    target(target_name)
        set_kind("static")
        set_policy("build.fence", true)
        add_packages("fmt")
        add_includedirs(incdirs())
        add_includedirs(out_dir_abs, {public = true})
        add_defines(gentest_common_defines())
        add_cxxflags(table.unpack(gentest_common_cxxflags()), {force = true})
        add_files(defs_cpp, anchor_cpp, {always_added = true})
        if opts.headerfiles and #opts.headerfiles > 0 then
            add_headerfiles(table.unpack(opts.headerfiles))
        else
            add_headerfiles(table.unpack(defs))
        end
        if opts.deps and #opts.deps > 0 then
            add_deps(table.unpack(opts.deps))
        end
        before_buildcmd(function (_, batchcmds)
            local codegen, compdb_dir = ensure_codegen(batchcmds)
            run_textual_mock_codegen(batchcmds, codegen, compdb_dir, config)
        end)

    return {
        target = target_name,
        include_dir = out_dir_abs,
        public_header = project_path(public_header),
    }
end

-- Attach textual suite codegen to a binary target and feed in the include roots
-- from any explicit textual mock targets.
function gentest_attach_codegen(opts)
    require_textual_kind(opts, "gentest_attach_codegen")

    local target_name = require_opt(opts, "name", "gentest_attach_codegen")
    local source = require_opt(opts, "source", "gentest_attach_codegen")
    local main_source = opts.main
    local output_dir = require_opt(opts, "output_dir", "gentest_attach_codegen")
    local out_dir_abs = project_path(output_dir)
    local source_basename = path.basename(source):gsub("%.[^.]+$", "")
    local wrapper_cpp = path.join(output_dir, "tu_0000_" .. source_basename .. ".gentest.cpp")
    local wrapper_h = path.join(output_dir, "tu_0000_" .. source_basename .. ".gentest.h")
    local wrapper_d = path.join(output_dir, "tu_0000_" .. source_basename .. ".gentest.d")
    local extra_includes = opts.includes or {}

    target(target_name)
        set_kind("binary")
        add_packages("fmt")
        add_includedirs(incdirs())
        for _, include_dir in ipairs(extra_includes) do
            add_includedirs(include_dir)
        end
        add_defines(gentest_common_defines())
        add_cxxflags(table.unpack(gentest_common_cxxflags()), {force = true})
        add_files(wrapper_cpp, {always_added = true})
        if main_source then
            add_files(main_source, {always_added = true})
        end
        if opts.deps and #opts.deps > 0 then
            add_deps(table.unpack(opts.deps))
        end
        before_buildcmd(function (_, batchcmds)
            local codegen, compdb_dir = ensure_codegen(batchcmds)

            local args = {
                buildsystem_codegen(),
                "--backend", "xmake",
                "--mode", "suite",
                "--kind", "textual",
                "--codegen", codegen,
                "--source-root", project_root(),
                "--out-dir", out_dir_abs,
                "--wrapper-output", project_path(wrapper_cpp),
                "--header-output", project_path(wrapper_h),
                "--depfile", project_path(wrapper_d),
                "--source-file", project_path(source),
            }
            if compdb_dir then
                table.insert(args, "--compdb")
                table.insert(args, compdb_dir)
            end
            append_common_codegen_clang_args(args, extra_includes)
            batchcmds:vrunv(python_program(), args)
        end)
end
