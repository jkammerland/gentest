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

local function require_kind(opts, operation)
    local kind = opts.kind or "textual"
    if kind ~= "textual" and kind ~= "modules" then
        raise(operation .. " only supports kind='textual' or kind='modules'")
    end
    return kind
end

local function sanitize_target_id(name)
    return (name:gsub("[^%w_]", "_"))
end

local function metadata_output_path(output_dir, target_id)
    return path.join(output_dir, target_id .. "_mock_metadata.json")
end

local function basename_stem(filepath)
    return path.basename(filepath):gsub("%.[^.]+$", "")
end

local function shorten_generated_stem(stem)
    local sanitized = stem:gsub("[^%w_]", "_")
    if sanitized == "" then
        sanitized = "tu"
    end
    if #sanitized <= 24 then
        return sanitized
    end
    return sanitized:sub(1, 16) .. "_" .. sanitized:sub(-7)
end

local function file_ext(filepath)
    local ext = path.extension(filepath)
    if ext == "" then
        return ".cppm"
    end
    return ext
end

local function module_suite_staged_source_rel(output_dir, source)
    return path.join(output_dir, "suite_0000" .. file_ext(source))
end

local function module_wrapper_output_rel(output_dir, source, index)
    return path.join(
        output_dir,
        string.format("tu_%04d_%s.module.gentest%s", index, shorten_generated_stem(basename_stem(source)), file_ext(source))
    )
end

local function module_header_output_rel(output_dir, source, index)
    return path.join(output_dir, string.format("tu_%04d_%s.gentest.h", index, shorten_generated_stem(basename_stem(source))))
end

local function module_defs_stage_rel(output_dir, defs_file, index)
    return path.join(output_dir, "defs", string.format("def_%04d_%s", index, path.filename(defs_file)))
end

local function module_public_output_rel(output_dir, module_name)
    local rel = module_name:gsub("%.", "/"):gsub(":", "/")
    return path.join(output_dir, rel .. ".cppm")
end

local function append_unique(result, seen, value)
    if value ~= nil and value ~= "" and not seen[value] then
        seen[value] = true
        table.insert(result, value)
    end
end

local function collect_dep_inputs(deps)
    local dep_targets = {}
    local include_dirs = {}
    local metadata_paths = {}
    local seen_targets = {}
    local seen_includes = {}
    local seen_metadata = {}
    for _, dep in ipairs(deps or {}) do
        if type(dep) == "table" then
            append_unique(dep_targets, seen_targets, dep.target)
            append_unique(metadata_paths, seen_metadata, dep.metadata_path)
            if dep.include_dir then
                append_unique(include_dirs, seen_includes, dep.include_dir)
            end
            for _, extra_include in ipairs(dep.include_dirs or {}) do
                append_unique(include_dirs, seen_includes, extra_include)
            end
        else
            append_unique(dep_targets, seen_targets, dep)
        end
    end
    return dep_targets, include_dirs, metadata_paths
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

local function run_mock_codegen(batchcmds, codegen, compdb_dir, config)
    local args = {
        buildsystem_codegen(),
        "--backend", "xmake",
        "--mode", "mocks",
        "--kind", config.kind,
        "--codegen", codegen,
        "--source-root", project_root(),
        "--out-dir", config.out_dir_abs,
        "--anchor-output", config.anchor_output,
        "--mock-registry", config.mock_registry,
        "--mock-impl", config.mock_impl,
        "--target-id", config.target_id,
        "--metadata-output", config.metadata_output,
    }
    if config.kind == "textual" then
        table.insert(args, "--wrapper-output")
        table.insert(args, config.wrapper_output)
        table.insert(args, "--header-output")
        table.insert(args, config.header_output)
        table.insert(args, "--public-header")
        table.insert(args, config.public_header)
    else
        table.insert(args, "--module-name")
        table.insert(args, config.module_name)
    end
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
    local kind = require_kind(opts, "gentest_add_mocks")

    local target_name = require_opt(opts, "name", "gentest_add_mocks")
    local output_dir = require_opt(opts, "output_dir", "gentest_add_mocks")
    local defs = require_opt(opts, "defs", "gentest_add_mocks")
    if type(defs) ~= "table" or #defs == 0 then
        raise("gentest_add_mocks requires `defs` to contain at least one file")
    end

    local target_id = opts.target_id or sanitize_target_id(target_name)
    local out_dir_abs = project_path(output_dir)
    local anchor_cpp = path.join(output_dir, target_id .. "_anchor.cpp")
    local mock_registry_h = path.join(output_dir, target_id .. "_mock_registry.hpp")
    local mock_impl_h = path.join(output_dir, target_id .. "_mock_impl.hpp")
    local metadata_json = path.join(output_dir, target_id .. "_mock_metadata.json")
    local config = {
        kind = kind,
        defs = {},
        out_dir_abs = out_dir_abs,
        anchor_output = project_path(anchor_cpp),
        mock_registry = project_path(mock_registry_h),
        mock_impl = project_path(mock_impl_h),
        target_id = target_id,
        metadata_output = project_path(metadata_json),
    }
    local add_public_files = {}
    local add_private_files = {}
    local include_dirs = {out_dir_abs}
    local public_surface = nil
    if kind == "textual" then
        local public_header_name = require_opt(opts, "header_name", "gentest_add_mocks")
        local defs_cpp = path.join(output_dir, target_id .. "_defs.cpp")
        local codegen_h = path.join(output_dir, "tu_0000_" .. target_id .. "_defs.gentest.h")
        local public_header = path.join(output_dir, public_header_name)
        config.wrapper_output = project_path(defs_cpp)
        config.header_output = project_path(codegen_h)
        config.public_header = project_path(public_header)
        add_private_files = {defs_cpp, anchor_cpp}
        public_surface = {type = "header", path = project_path(public_header)}
    else
        local module_name = require_opt(opts, "module_name", "gentest_add_mocks")
        config.module_name = module_name
        for index, defs_file in ipairs(defs) do
            local zero_index = index - 1
            local staged_rel = module_defs_stage_rel(output_dir, defs_file, zero_index)
            local wrapper_rel = module_wrapper_output_rel(output_dir, staged_rel, zero_index)
            table.insert(add_public_files, wrapper_rel)
        end
        local public_module = module_public_output_rel(output_dir, module_name)
        table.insert(add_public_files, public_module)
        table.insert(add_private_files, anchor_cpp)
        public_surface = {type = "module", module_name = module_name, path = project_path(public_module)}
    end
    for _, defs_file in ipairs(defs) do
        table.insert(config.defs, project_path(defs_file))
    end
    local dep_targets, dep_include_dirs = collect_dep_inputs(opts.deps)

    target(target_name)
        set_kind("static")
        set_policy("build.fence", true)
        add_packages("fmt")
        add_includedirs(incdirs())
        add_includedirs(out_dir_abs, {public = true})
        for _, include_dir in ipairs(dep_include_dirs) do
            add_includedirs(include_dir)
        end
        add_defines(gentest_common_defines())
        add_cxxflags(table.unpack(gentest_common_cxxflags()), {force = true})
        if #add_private_files > 0 then
            add_files(table.unpack(add_private_files), {always_added = true})
        end
        if #add_public_files > 0 then
            add_files(table.unpack(add_public_files), {public = true, always_added = true})
        end
        if opts.headerfiles and #opts.headerfiles > 0 then
            add_headerfiles(table.unpack(opts.headerfiles))
        else
            add_headerfiles(table.unpack(defs))
        end
        if #dep_targets > 0 then
            add_deps(table.unpack(dep_targets))
        end
        before_buildcmd(function (_, batchcmds)
            local codegen, compdb_dir = ensure_codegen(batchcmds)
            run_mock_codegen(batchcmds, codegen, compdb_dir, config)
        end)

    if kind == "textual" then
        return {
            target = target_name,
            include_dir = out_dir_abs,
            include_dirs = include_dirs,
            public_header = public_surface.path,
            metadata_path = config.metadata_output,
        }
    end
    return {
        target = target_name,
        include_dir = out_dir_abs,
        include_dirs = include_dirs,
        module_name = public_surface.module_name,
        public_module = public_surface.path,
        metadata_path = config.metadata_output,
    }
end

function gentest_attach_codegen(opts)
    local kind = require_kind(opts, "gentest_attach_codegen")

    local target_name = require_opt(opts, "name", "gentest_attach_codegen")
    local source = require_opt(opts, "source", "gentest_attach_codegen")
    local main_source = opts.main
    local output_dir = require_opt(opts, "output_dir", "gentest_attach_codegen")
    local out_dir_abs = project_path(output_dir)
    local wrapper_cpp = nil
    local wrapper_h = nil
    if kind == "textual" then
        local source_basename = path.basename(source):gsub("%.[^.]+$", "")
        wrapper_cpp = path.join(output_dir, "tu_0000_" .. source_basename .. ".gentest.cpp")
        wrapper_h = path.join(output_dir, "tu_0000_" .. source_basename .. ".gentest.h")
    else
        local staged_rel = module_suite_staged_source_rel(output_dir, source)
        wrapper_cpp = module_wrapper_output_rel(output_dir, staged_rel, 0)
        wrapper_h = module_header_output_rel(output_dir, staged_rel, 0)
    end
    local wrapper_d = path.join(output_dir, basename_stem(wrapper_h) .. ".d")
    local extra_includes = {}
    local seen_extra_includes = {}
    for _, include_dir in ipairs(opts.includes or {}) do
        append_unique(extra_includes, seen_extra_includes, include_dir)
    end
    local dep_targets, dep_include_dirs, metadata_paths = collect_dep_inputs(opts.deps)
    for _, include_dir in ipairs(dep_include_dirs) do
        append_unique(extra_includes, seen_extra_includes, include_dir)
    end

    target(target_name)
        set_kind("binary")
        add_packages("fmt")
        add_includedirs(incdirs())
        for _, include_dir in ipairs(extra_includes) do
            add_includedirs(include_dir)
        end
        add_defines(gentest_common_defines())
        if opts.defines and #opts.defines > 0 then
            add_defines(table.unpack(opts.defines))
        end
        add_cxxflags(table.unpack(gentest_common_cxxflags()), {force = true})
        if kind == "modules" then
            add_files(wrapper_cpp, {public = true, always_added = true})
        else
            add_files(wrapper_cpp, {always_added = true})
        end
        if main_source then
            add_files(main_source, {always_added = true})
        end
        if #dep_targets > 0 then
            add_deps(table.unpack(dep_targets))
        end
        before_buildcmd(function (_, batchcmds)
            local codegen, compdb_dir = ensure_codegen(batchcmds)

            local args = {
                buildsystem_codegen(),
                "--backend", "xmake",
                "--mode", "suite",
                "--kind", kind,
                "--codegen", codegen,
                "--source-root", project_root(),
                "--out-dir", out_dir_abs,
                "--wrapper-output", project_path(wrapper_cpp),
                "--header-output", project_path(wrapper_h),
                "--depfile", project_path(wrapper_d),
                "--source-file", project_path(source),
            }
            if kind == "modules" then
                for _, include_dir in ipairs(resolved_incdirs()) do
                    table.insert(args, "--include-root")
                    table.insert(args, include_dir)
                end
            end
            for _, metadata_path in ipairs(metadata_paths) do
                table.insert(args, "--mock-metadata")
                table.insert(args, metadata_path)
            end
            if compdb_dir then
                table.insert(args, "--compdb")
                table.insert(args, compdb_dir)
            end
            append_common_codegen_clang_args(args, extra_includes)
            batchcmds:vrunv(python_program(), args)
        end)
end
