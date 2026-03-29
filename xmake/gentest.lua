local gentest_state = {}
local fail

fail = function(message)
    print("error: gentest xmake: " .. message)
    os.exit(1)
end

-- Configure the shared Xmake helper context. External consumers can override
-- codegen_project_root to point at a gentest checkout, or provide
-- codegen = { exe = ..., clang = ..., scan_deps = ... }.
function gentest_configure(opts)
    gentest_state = opts or {}
end

local function state_value(key)
    local value = gentest_state[key]
    if value == nil then
        fail("gentest_configure must provide `" .. key .. "`")
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

local function configured_build_dir()
    local builddir = get_config("builddir") or get_config("buildir") or "build"
    return project_path(builddir)
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
    local configured = gentest_state["codegen_project_root"] or project_root()
    if os.isfile(path.join(configured, "CMakeLists.txt")) then
        return configured
    end
    local oldpwd = os.getenv("OLDPWD")
    if oldpwd and os.isfile(path.join(oldpwd, "CMakeLists.txt")) then
        return oldpwd
    end
    if os.readlink then
        for _, link_name in ipairs({"xmake", "scripts"}) do
            local link_path = path.join(project_root(), link_name)
            local ok, resolved = pcall(os.readlink, link_path)
            if ok and resolved and resolved ~= "" then
                return path.directory(resolved)
            end
        end
    end
    return configured
end

local function gentest_common_defines()
    return state_value("gentest_common_defines")
end

local function gentest_common_cxxflags()
    return state_value("gentest_common_cxxflags")
end

local function registered_target_metadata()
    local metadata = gentest_state["registered_target_metadata"]
    if metadata == nil then
        metadata = {}
        gentest_state["registered_target_metadata"] = metadata
    end
    return metadata
end

local function append_codegen_define_args(args, defines, seen_defines)
    for _, define in ipairs(defines or {}) do
        local define_arg = tostring(define or "")
        if define_arg ~= "" then
            if not define_arg:match("^[-/]D") then
                define_arg = "-D" .. define_arg
            end
            if not seen_defines[define_arg] then
                seen_defines[define_arg] = true
                table.insert(args, "--clang-arg=" .. define_arg)
            end
        end
    end
end

local function append_raw_define_args(args, defines, seen_defines)
    for _, define in ipairs(defines or {}) do
        local define_arg = tostring(define or "")
        if define_arg ~= "" then
            if not define_arg:match("^[-/]D") then
                define_arg = "-D" .. define_arg
            end
            if not seen_defines[define_arg] then
                seen_defines[define_arg] = true
                table.insert(args, define_arg)
            end
        end
    end
end

local function append_user_clang_args(args, clang_args)
    for _, clang_arg in ipairs(clang_args or {}) do
        local arg_text = tostring(clang_arg or "")
        if arg_text ~= "" then
            if arg_text:find("^%-%-clang%-arg=") then
                table.insert(args, arg_text)
            else
                table.insert(args, "--clang-arg=" .. arg_text)
            end
        end
    end
end

local function append_raw_user_clang_args(args, clang_args)
    for _, clang_arg in ipairs(clang_args or {}) do
        local arg_text = tostring(clang_arg or "")
        if arg_text ~= "" then
            if arg_text:find("^%-%-clang%-arg=") then
                table.insert(args, arg_text:sub(13))
            else
                table.insert(args, arg_text)
            end
        end
    end
end

local function append_common_codegen_clang_args(args, extra_include_dirs, extra_defines, extra_clang_args)
    table.insert(args, "--clang-arg=-std=c++20")
    table.insert(args, "--clang-arg=-DGENTEST_CODEGEN=1")
    local seen_defines = {}
    append_codegen_define_args(args, gentest_common_defines(), seen_defines)
    append_codegen_define_args(args, extra_defines, seen_defines)
    table.insert(args, "--clang-arg=-Wno-unknown-attributes")
    table.insert(args, "--clang-arg=-Wno-attributes")
    table.insert(args, "--clang-arg=-Wno-unknown-warning-option")
    for _, include_dir in ipairs(resolved_incdirs()) do
        table.insert(args, "--clang-arg=-I" .. include_dir)
    end
    for _, include_dir in ipairs(extra_include_dirs or {}) do
        table.insert(args, "--clang-arg=-I" .. project_path(include_dir))
    end
    append_user_clang_args(args, extra_clang_args)
end

local function append_common_codegen_driver_args(args, extra_include_dirs, extra_defines, extra_clang_args)
    local seen_defines = {}
    table.insert(args, "--")
    table.insert(args, "-std=c++20")
    table.insert(args, "-DGENTEST_CODEGEN=1")
    append_raw_define_args(args, gentest_common_defines(), seen_defines)
    append_raw_define_args(args, extra_defines, seen_defines)
    table.insert(args, "-Wno-unknown-attributes")
    table.insert(args, "-Wno-attributes")
    table.insert(args, "-Wno-unknown-warning-option")
    for _, include_dir in ipairs(resolved_incdirs()) do
        table.insert(args, "-I" .. include_dir)
    end
    for _, include_dir in ipairs(extra_include_dirs or {}) do
        table.insert(args, "-I" .. project_path(include_dir))
    end
    append_raw_user_clang_args(args, gentest_common_cxxflags())
    append_raw_user_clang_args(args, extra_clang_args)
end

local function default_external_module_sources()
    return {
        "gentest=" .. project_path("include/gentest/gentest.cppm"),
        "gentest.mock=" .. project_path("include/gentest/gentest.mock.cppm"),
        "gentest.bench_util=" .. project_path("include/gentest/gentest.bench_util.cppm"),
    }
end

local function require_opt(opts, key, operation)
    local value = opts[key]
    if value == nil or value == "" then
        fail(operation .. " requires `" .. key .. "`")
    end
    return value
end

local function require_kind(opts, operation)
    local kind = opts.kind or "textual"
    if kind ~= "textual" and kind ~= "modules" then
        fail(operation .. " only supports kind='textual' or kind='modules'")
    end
    return kind
end

local function sanitize_target_id(name)
    return (name:gsub("[^%w_]", "_"))
end

local function basename_stem(filepath)
    return (path.filename(filepath) or path.basename(filepath)):gsub("%.[^.]+$", "")
end

local shorten_stem_digest_cache = {}
local shorten_stem_shift_amounts = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
}
local shorten_stem_md5_constants = {}
for index = 1, 64 do
    shorten_stem_md5_constants[index] = math.floor(math.abs(math.sin(index)) * 4294967296) & 0xffffffff
end

local function shorten_stem_left_rotate(value, shift)
    return (((value & 0xffffffff) << shift) | ((value & 0xffffffff) >> (32 - shift))) & 0xffffffff
end

local function shorten_stem_read_le_word(text, offset)
    local b1, b2, b3, b4 = text:byte(offset, offset + 3)
    return ((b1 or 0) | ((b2 or 0) << 8) | ((b3 or 0) << 16) | ((b4 or 0) << 24)) & 0xffffffff
end

local function shorten_stem_word_hex_le(word)
    return string.format("%02x%02x%02x%02x", word & 0xff, (word >> 8) & 0xff, (word >> 16) & 0xff, (word >> 24) & 0xff)
end

local function shorten_stem_digest(text)
    local cached = shorten_stem_digest_cache[text]
    if cached then
        return cached
    end
    local message = text .. string.char(0x80)
    local pad_len = (56 - (#message % 64)) % 64
    if pad_len > 0 then
        message = message .. string.rep("\0", pad_len)
    end
    local bit_length = #text * 8
    local low = bit_length & 0xffffffff
    local high = math.floor(bit_length / 4294967296) & 0xffffffff
    message = message ..
                  string.char(low & 0xff, (low >> 8) & 0xff, (low >> 16) & 0xff, (low >> 24) & 0xff, high & 0xff,
                              (high >> 8) & 0xff, (high >> 16) & 0xff, (high >> 24) & 0xff)

    local a0 = 0x67452301
    local b0 = 0xefcdab89
    local c0 = 0x98badcfe
    local d0 = 0x10325476

    for chunk_start = 1, #message, 64 do
        local words = {}
        for word_index = 0, 15 do
            words[word_index] = shorten_stem_read_le_word(message, chunk_start + word_index * 4)
        end

        local a = a0
        local b = b0
        local c = c0
        local d = d0

        for round_index = 0, 63 do
            local f = 0
            local g = 0
            if round_index < 16 then
                f = (b & c) | ((~b) & d)
                g = round_index
            elseif round_index < 32 then
                f = (d & b) | ((~d) & c)
                g = (5 * round_index + 1) % 16
            elseif round_index < 48 then
                f = b ~ c ~ d
                g = (3 * round_index + 5) % 16
            else
                f = c ~ (b | (~d))
                g = (7 * round_index) % 16
            end

            f = (f + a + shorten_stem_md5_constants[round_index + 1] + words[g]) & 0xffffffff
            a, b, c, d = d, (b + shorten_stem_left_rotate(f, shorten_stem_shift_amounts[round_index + 1])) & 0xffffffff, b, c
        end

        a0 = (a0 + a) & 0xffffffff
        b0 = (b0 + b) & 0xffffffff
        c0 = (c0 + c) & 0xffffffff
        d0 = (d0 + d) & 0xffffffff
    end

    local digest = shorten_stem_word_hex_le(a0) .. shorten_stem_word_hex_le(b0) .. shorten_stem_word_hex_le(c0) ..
                       shorten_stem_word_hex_le(d0)
    shorten_stem_digest_cache[text] = digest
    return digest
end

local function anchor_symbol_name(target_id)
    local sanitized = sanitize_target_id(target_id)
    if sanitized == "" or sanitized:match("^[0-9]") then
        sanitized = "_" .. sanitized
    end
    return sanitized .. "_" .. shorten_stem_digest(target_id):sub(1, 12) .. "_explicit_mock_anchor"
end

local function shorten_generated_stem(stem)
    local sanitized = stem:gsub("[^%w_]", "_")
    if sanitized == "" then
        sanitized = "tu"
    end
    if #sanitized <= 24 then
        return sanitized
    end
    return sanitized:sub(1, 16) .. "_" .. shorten_stem_digest(sanitized):sub(1, 8)
end

local function file_ext(filepath)
    local ext = path.extension(filepath)
    if ext == "" then
        return ".cppm"
    end
    return ext
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

local function module_public_output_rel(output_dir, module_name)
    local rel = module_name:gsub("%.", "/"):gsub(":", "/")
    return path.join(output_dir, rel .. ".cppm")
end

local function template_path(filename)
    return project_path(path.join("xmake", "templates", filename))
end

local function append_unique(result, seen, value)
    if value ~= nil and value ~= "" and not seen[value] then
        seen[value] = true
        table.insert(result, value)
    end
end

local function configured_codegen_settings()
    local codegen = gentest_state["codegen"]
    if codegen == nil then
        return {}
    end
    if type(codegen) ~= "table" then
        fail("gentest_configure `codegen` must be a table when provided")
    end
    return codegen
end

local function resolve_program_candidate(candidate)
    local candidate_text = tostring(candidate or "")
    if candidate_text == "" then
        return nil
    end
    if os.isfile(candidate_text) then
        return candidate_text
    end
    if not path.is_absolute(candidate_text) then
        for _, base_dir in ipairs({project_root(), codegen_project_root()}) do
            local joined = path.join(base_dir, candidate_text)
            if os.isfile(joined) then
                return joined
            end
        end
    end
    if not candidate_text:find("[/\\]") then
        local tool = find_tool(candidate_text, {force = true})
        if tool and tool.program and os.isfile(tool.program) then
            return tool.program
        end
    end
    return nil
end

local function resolve_explicit_program(candidate, source_label, description)
    if candidate == nil or candidate == "" then
        return nil
    end
    local resolved = resolve_program_candidate(candidate)
    if resolved then
        return resolved
    end
    fail("failed to resolve " .. description .. " from " .. source_label .. ": `" .. tostring(candidate) .. "`")
end

local function find_nearby_compile_commands(program_path)
    if not program_path or program_path == "" then
        return nil
    end
    local candidate = path.directory(program_path)
    local remaining = 8
    while candidate and remaining > 0 do
        if os.isfile(path.join(candidate, "compile_commands.json")) then
            return candidate
        end
        local parent = path.directory(candidate)
        if not parent or parent == candidate then
            break
        end
        candidate = parent
        remaining = remaining - 1
    end
    return nil
end

local function relative_include(from_relpath, to_relpath)
    return path.translate(path.relative(project_path(to_relpath), path.directory(project_path(from_relpath))))
end

local function include_lines(from_relpath, relpaths)
    local lines = {}
    for _, relpath in ipairs(relpaths or {}) do
        table.insert(lines, "#include \"" .. relative_include(from_relpath, relpath) .. "\"")
    end
    return table.concat(lines, "\n")
end

local function export_import_lines(module_names)
    local lines = {}
    for _, module_name in ipairs(module_names or {}) do
        table.insert(lines, "export import " .. module_name .. ";")
    end
    return table.concat(lines, "\n")
end

local function add_generated_template(template_name, output_rel, variables)
    add_configfiles(template_path(template_name), {
        filename = output_rel,
        variables = variables or {},
    })
end

local function add_generated_copy(source_relpath, output_rel)
    add_configfiles(project_path(source_relpath), {
        filename = output_rel,
        onlycopy = true,
    })
end

local function collect_dep_targets(deps)
    local dep_targets = {}
    local seen_targets = {}
    for _, dep in ipairs(deps or {}) do
        if type(dep) == "table" then
            if dep.target then
                append_unique(dep_targets, seen_targets, dep.target)
            end
        else
            append_unique(dep_targets, seen_targets, dep)
        end
    end
    return dep_targets
end

local function resolve_dep_inputs(deps)
    local include_dirs = {}
    local seen_includes = {}
    local seen_targets = {}
    local metadata_by_target = registered_target_metadata()

    local function visit_dep(dep)
        if type(dep) == "table" then
            if dep.include_dir then
                append_unique(include_dirs, seen_includes, dep.include_dir)
            end
            for _, extra_include in ipairs(dep.include_dirs or {}) do
                append_unique(include_dirs, seen_includes, extra_include)
            end
            dep = dep.target
        end
        if dep and not seen_targets[dep] then
            seen_targets[dep] = true
            local registered = metadata_by_target[dep]
            if registered then
                if registered.include_dir then
                    append_unique(include_dirs, seen_includes, registered.include_dir)
                end
                for _, extra_include in ipairs(registered.include_dirs or {}) do
                    append_unique(include_dirs, seen_includes, extra_include)
                end
                for _, nested_dep in ipairs(registered.deps or {}) do
                    visit_dep(nested_dep)
                end
            end
        end
    end

    for _, dep in ipairs(deps or {}) do
        visit_dep(dep)
    end
    return include_dirs
end

local function collect_mock_metadata_inputs(deps)
    local include_dirs = {}
    local module_sources = {}
    local support_headers = {}
    local seen_includes = {}
    local seen_modules = {}
    local seen_headers = {}
    local seen_targets = {}
    local metadata_by_target = registered_target_metadata()

    local function merge_payload(payload)
        if not payload then
            return
        end
        for _, include_dir in ipairs(payload.include_dirs or {}) do
            append_unique(include_dirs, seen_includes, include_dir)
        end
        for _, module_source in ipairs(payload.module_sources or {}) do
            if type(module_source) == "table" then
                local module_name = module_source.module_name or ""
                local module_path = module_source.path or ""
                if module_name ~= "" and module_path ~= "" then
                    append_unique(module_sources, seen_modules, module_name .. "=" .. module_path)
                end
            end
        end
        for _, support_header in ipairs(payload.support_headers or {}) do
            append_unique(support_headers, seen_headers, support_header)
        end
    end

    local function visit_dep(dep)
        if type(dep) == "table" and dep.mock_metadata then
            merge_payload(dep.mock_metadata)
            dep = dep.target
        end
        if dep and not seen_targets[dep] then
            seen_targets[dep] = true
            local registered = metadata_by_target[dep]
            if registered then
                merge_payload(registered.mock_metadata)
                for _, nested_dep in ipairs(registered.deps or {}) do
                    visit_dep(nested_dep)
                end
            end
        end
    end

    for _, dep in ipairs(deps or {}) do
        visit_dep(dep)
    end
    return include_dirs, module_sources, support_headers
end

local function collect_target_package_include_dirs(target)
    local include_dirs = {}
    local seen_includes = {}
    if not target or not target.orderpkgs then
        return include_dirs
    end
    for _, pkg in ipairs(target:orderpkgs() or {}) do
        for _, include_dir in ipairs(pkg:get("sysincludedirs") or {}) do
            append_unique(include_dirs, seen_includes, include_dir)
        end
        for _, include_dir in ipairs(pkg:get("includedirs") or {}) do
            append_unique(include_dirs, seen_includes, include_dir)
        end
    end
    return include_dirs
end

local function textual_mock_metadata_payload(config)
    return {
        schema_version = 1,
        mode = "mocks",
        backend = "xmake",
        kind = "textual",
        target_id = config.target_id,
        out_dir = config.out_dir_abs,
        include_dirs = {config.out_dir_abs},
        public_surface = {
            type = "header",
            path = config.public_header,
        },
        module_sources = {},
        support_headers = {
            config.public_header,
            config.mock_registry,
            config.mock_impl,
        },
    }
end

local function module_mock_metadata_payload(config)
    local module_sources = {}
    for index, module_name in ipairs(config.defs_modules or {}) do
        table.insert(module_sources, {
            module_name = module_name,
            path = config.module_wrapper_outputs[index],
        })
    end
    table.insert(module_sources, {
        module_name = config.module_name,
        path = config.public_module,
    })
    return {
        schema_version = 1,
        mode = "mocks",
        backend = "xmake",
        kind = "modules",
        target_id = config.target_id,
        out_dir = config.out_dir_abs,
        include_dirs = {config.out_dir_abs},
        public_surface = {
            type = "module",
            path = config.public_module,
            module_name = config.module_name,
        },
        module_sources = module_sources,
        support_headers = {
            config.mock_registry,
            config.mock_impl,
        },
    }
end

local function is_clang_tool(toolpath)
    local toolname = path.filename(toolpath or ""):lower()
    toolname = toolname:gsub("%.exe$", "")
    return toolname == "clang++" or toolname == "clang" or toolname == "clang-cl" or toolname:match("^clang%+%+%-%d+$") or
               toolname:match("^clang%-%d+$")
end

local function scan_deps_candidate_names(host_clang)
    local candidates = {}
    local toolname = path.filename(host_clang or ""):lower():gsub("%.exe$", "")
    local version_suffix = toolname:match("^clang%+%+%-(%d+)$") or toolname:match("^clang%-(%d+)$")
    if version_suffix and version_suffix ~= "" then
        if is_host("windows") then
            table.insert(candidates, "clang-scan-deps-" .. version_suffix .. ".exe")
        else
            table.insert(candidates, "clang-scan-deps-" .. version_suffix)
        end
    end
    if is_host("windows") then
        table.insert(candidates, "clang-scan-deps.exe")
    else
        table.insert(candidates, "clang-scan-deps")
    end
    return candidates
end

local function configured_cxx_tool_hint()
    return get_config("cxx") or os.getenv("CXX") or ""
end

local function resolve_codegen_host_clang(target)
    local configured_codegen = configured_codegen_settings()
    local configured_host_clang = resolve_explicit_program(
        configured_codegen["clang"],
        "`gentest_configure().codegen.clang`",
        "host Clang executable"
    )
    if configured_host_clang then
        return configured_host_clang
    end
    local env_host_clang = os.getenv("GENTEST_CODEGEN_HOST_CLANG")
    if env_host_clang and env_host_clang ~= "" then
        return resolve_explicit_program(env_host_clang, "`GENTEST_CODEGEN_HOST_CLANG`", "host Clang executable")
    end
    local cxx_tool = target and target.tool and target:tool("cxx") or ""
    if cxx_tool == "" then
        cxx_tool = configured_cxx_tool_hint()
    end
    if cxx_tool ~= "" and is_clang_tool(cxx_tool) then
        return resolve_program_candidate(cxx_tool) or cxx_tool
    end
    return nil
end

local function resolve_codegen_scan_deps(host_clang)
    local configured_codegen = configured_codegen_settings()
    local configured_scan_deps = resolve_explicit_program(
        configured_codegen["scan_deps"],
        "`gentest_configure().codegen.scan_deps`",
        "clang-scan-deps executable"
    )
    if configured_scan_deps then
        return configured_scan_deps
    end
    local env_scan_deps = os.getenv("GENTEST_CODEGEN_CLANG_SCAN_DEPS")
    if env_scan_deps and env_scan_deps ~= "" then
        return resolve_explicit_program(env_scan_deps, "`GENTEST_CODEGEN_CLANG_SCAN_DEPS`", "clang-scan-deps executable")
    end
    if host_clang and host_clang ~= "" then
        local bin_dir = path.directory(host_clang)
        for _, candidate_name in ipairs(scan_deps_candidate_names(host_clang)) do
            local candidate = path.join(bin_dir, candidate_name)
            if os.isfile(candidate) then
                return candidate
            end
        end
    end
    return nil
end

local function require_clang_module_toolchain(target, operation)
    local cxx_tool = target and target.tool and target:tool("cxx") or ""
    if cxx_tool == "" then
        cxx_tool = configured_cxx_tool_hint()
        if cxx_tool == "" then
            return
        end
    end
    if is_clang_tool(cxx_tool) then
        return
    end
    fail(
        operation .. "(kind='modules') requires a Clang C++ target toolchain in Xmake. "
            .. "Configure the target toolchain/compiler with Clang for module compilation, and configure codegen host tools "
            .. "separately with gentest_configure({ codegen = { clang = ..., scan_deps = ... }}) or "
            .. "GENTEST_CODEGEN_HOST_CLANG / GENTEST_CODEGEN_CLANG_SCAN_DEPS. "
            .. "Resolved cxx tool: `" .. tostring(cxx_tool) .. "`"
    )
end

local function run_command(batchcmds, program, args)
    if batchcmds then
        batchcmds:vrunv(program, args)
    end
end

local function resolve_codegen()
    local configured_codegen = configured_codegen_settings()
    local explicit_codegen = resolve_explicit_program(
        configured_codegen["exe"],
        "`gentest_configure().codegen.exe`",
        "gentest_codegen executable"
    )
    if explicit_codegen then
        return explicit_codegen, find_nearby_compile_commands(explicit_codegen), nil
    end

    local env_path = os.getenv("GENTEST_CODEGEN")
    if env_path then
        local resolved_env_path = resolve_program_candidate(env_path)
        if resolved_env_path and os.isfile(resolved_env_path) then
            return resolved_env_path, find_nearby_compile_commands(resolved_env_path), nil
        end
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

local function existing_project_compdb_dir()
    local build_dir = configured_build_dir()
    if os.isfile(path.join(build_dir, "compile_commands.json")) then
        return build_dir
    end
    return nil
end

local function ensure_codegen(batchcmds, target)
    local cached = gentest_state["_resolved_codegen"]
    if cached and cached.path and os.isfile(cached.path) then
        local host_clang = resolve_codegen_host_clang(target)
        return cached.path, cached.compdb_dir, host_clang, resolve_codegen_scan_deps(host_clang)
    end

    local codegen, compdb_dir, cmake_build_dir = resolve_codegen()
    if cmake_build_dir and not os.isfile(codegen) then
        run_command(batchcmds, "cmake", {"-S", codegen_project_root(), "-B", cmake_build_dir, "-DCMAKE_BUILD_TYPE=Release",
                                         "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"})
        run_command(batchcmds, "cmake", {"--build", cmake_build_dir, "--target", "gentest_codegen", "-j", "1"})
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

    local project_compdb_dir = existing_project_compdb_dir()
    if project_compdb_dir then
        compdb_dir = project_compdb_dir
    end

    gentest_state["_resolved_codegen"] = {path = codegen, compdb_dir = compdb_dir}
    local host_clang = resolve_codegen_host_clang(target)
    return codegen, compdb_dir, host_clang, resolve_codegen_scan_deps(host_clang)
end

local function run_mock_codegen(batchcmds, codegen, compdb_dir, host_clang, scan_deps, config)
    local args = {
        "--source-root", project_root(),
        "--tu-out-dir", config.out_dir_abs,
        "--mock-registry", config.mock_registry,
        "--mock-impl", config.mock_impl,
        "--discover-mocks",
    }
    if config.kind == "textual" then
        table.insert(args, "--tu-header-output")
        table.insert(args, config.header_output)
        table.insert(args, config.wrapper_output)
    else
        for _, header_output in ipairs(config.module_header_outputs or {}) do
            table.insert(args, "--tu-header-output")
            table.insert(args, header_output)
        end
        if compdb_dir then
            table.insert(args, "--compdb")
            table.insert(args, compdb_dir)
        end
        for _, module_source in ipairs(default_external_module_sources()) do
            table.insert(args, "--external-module-source")
            table.insert(args, module_source)
        end
        for _, module_source in ipairs(config.dep_module_sources or {}) do
            table.insert(args, "--external-module-source")
            table.insert(args, module_source)
        end
        for _, defs_file in ipairs(config.defs or {}) do
            table.insert(args, defs_file)
        end
    end
    if host_clang and host_clang ~= "" then
        table.insert(args, "--host-clang")
        table.insert(args, host_clang)
    end
    if scan_deps and scan_deps ~= "" then
        table.insert(args, "--clang-scan-deps")
        table.insert(args, scan_deps)
    end
    if config.kind == "textual" and compdb_dir then
        table.insert(args, "--compdb")
        table.insert(args, compdb_dir)
    end
    append_common_codegen_driver_args(args, config.extra_includes, config.defines, config.clang_args)
    run_command(batchcmds, codegen, args)
end

local function run_suite_codegen(batchcmds, codegen, compdb_dir, host_clang, scan_deps, config)
    local args = {
        "--source-root", project_root(),
        "--tu-out-dir", config.out_dir_abs,
        "--tu-header-output", config.header_output,
    }
    if config.depfile and config.depfile ~= "" then
        table.insert(args, "--depfile")
        table.insert(args, config.depfile)
    end
    if config.kind == "modules" then
        if compdb_dir then
            table.insert(args, "--compdb")
            table.insert(args, compdb_dir)
        end
        for _, module_source in ipairs(default_external_module_sources()) do
            table.insert(args, "--external-module-source")
            table.insert(args, module_source)
        end
        for _, module_source in ipairs(config.dep_module_sources or {}) do
            table.insert(args, "--external-module-source")
            table.insert(args, module_source)
        end
        table.insert(args, config.source_file)
    else
        if compdb_dir then
            table.insert(args, "--compdb")
            table.insert(args, compdb_dir)
        end
        table.insert(args, config.source_file)
    end
    if host_clang and host_clang ~= "" then
        table.insert(args, "--host-clang")
        table.insert(args, host_clang)
    end
    if scan_deps and scan_deps ~= "" then
        table.insert(args, "--clang-scan-deps")
        table.insert(args, scan_deps)
    end
    append_common_codegen_driver_args(args, config.extra_includes, config.defines, config.clang_args)
    run_command(batchcmds, codegen, args)
end

function gentest_add_mocks(opts)
    local kind = require_kind(opts, "gentest_add_mocks")
    if kind == "modules" then
        require_clang_module_toolchain(nil, "gentest_add_mocks")
    end

    local target_name = require_opt(opts, "name", "gentest_add_mocks")
    local output_dir = require_opt(opts, "output_dir", "gentest_add_mocks")
    local defs = require_opt(opts, "defs", "gentest_add_mocks")
    if type(defs) ~= "table" or #defs == 0 then
        fail("gentest_add_mocks requires `defs` to contain at least one file")
    end
    local defs_modules = nil
    if kind == "modules" then
        defs_modules = require_opt(opts, "defs_modules", "gentest_add_mocks")
        if type(defs_modules) ~= "table" or #defs_modules ~= #defs then
            fail("gentest_add_mocks(kind='modules') requires `defs_modules` with one explicit module name per defs file")
        end
    end

    local target_id = opts.target_id or sanitize_target_id(target_name)
    local out_dir_abs = project_path(output_dir)
    local anchor_cpp = path.join(output_dir, target_id .. "_anchor.cpp")
    local mock_registry_h = path.join(output_dir, target_id .. "_mock_registry.hpp")
    local mock_impl_h = path.join(output_dir, target_id .. "_mock_impl.hpp")
    local config = {
        kind = kind,
        defs = {},
        out_dir_abs = out_dir_abs,
        anchor_output = project_path(anchor_cpp),
        mock_registry = project_path(mock_registry_h),
        mock_impl = project_path(mock_impl_h),
        target_id = target_id,
        extra_includes = {},
        dep_module_sources = {},
        deps = opts.deps or {},
        defines = opts.defines or {},
        clang_args = opts.clang_args or {},
        defs_modules = defs_modules or {},
    }
    local add_public_files = {}
    local add_private_files = {}
    local include_dirs = {out_dir_abs}
    local seen_registered_includes = {[out_dir_abs] = true}
    local defs_cpp = nil
    local codegen_h = nil
    local public_header = nil
    local public_module = nil
    if kind == "textual" then
        local public_header_name = require_opt(opts, "header_name", "gentest_add_mocks")
        defs_cpp = path.join(output_dir, target_id .. "_defs.cpp")
        codegen_h = path.join(output_dir, "tu_0000_" .. target_id .. "_defs.gentest.h")
        public_header = path.join(output_dir, public_header_name)
        config.wrapper_output = project_path(defs_cpp)
        config.header_output = project_path(codegen_h)
        config.public_header = project_path(public_header)
        add_private_files = {defs_cpp, anchor_cpp}
    else
        local module_name = require_opt(opts, "module_name", "gentest_add_mocks")
        config.module_name = module_name
        config.module_wrapper_outputs = {}
        config.module_header_outputs = {}
        for index, defs_file in ipairs(defs) do
            local zero_index = index - 1
            local wrapper_rel = module_wrapper_output_rel(output_dir, defs_file, zero_index)
            local header_rel = module_header_output_rel(output_dir, defs_file, zero_index)
            table.insert(config.module_wrapper_outputs, project_path(wrapper_rel))
            table.insert(config.module_header_outputs, project_path(header_rel))
            table.insert(add_public_files, wrapper_rel)
        end
        public_module = module_public_output_rel(output_dir, module_name)
        config.public_module = project_path(public_module)
        table.insert(add_private_files, anchor_cpp)
    end
    for _, defs_file in ipairs(defs) do
        table.insert(config.defs, project_path(defs_file))
    end
    local dep_targets = collect_dep_targets(opts.deps)

    set_policy("build.fence", true)
    set_configdir(project_root())
    add_packages("fmt")
    add_includedirs(incdirs())
    add_includedirs(out_dir_abs, {public = true})
    add_defines(gentest_common_defines())
    if opts.defines and #opts.defines > 0 then
        add_defines(table.unpack(opts.defines))
    end
    add_cxxflags(table.unpack(gentest_common_cxxflags()), {force = true})
    if opts.clang_args and #opts.clang_args > 0 then
        add_cxxflags(table.unpack(opts.clang_args), {force = true})
    end
    for _, private_file in ipairs(add_private_files) do
        add_files(private_file, {always_added = true})
    end
    for _, public_file in ipairs(add_public_files) do
        add_files(public_file, {public = true, always_added = true})
    end
    if public_module then
        add_files(public_module, {public = true, always_added = true})
    end
    if public_header then
        add_headerfiles(public_header, {public = true})
    end
    if opts.headerfiles and #opts.headerfiles > 0 then
        add_headerfiles(table.unpack(opts.headerfiles))
    else
        add_headerfiles(table.unpack(defs))
    end
    if #dep_targets > 0 then
        add_deps(table.unpack(dep_targets))
    end
    add_generated_template("anchor.cpp.in", anchor_cpp, {
        ANCHOR_SYMBOL = anchor_symbol_name(target_id),
    })
    add_generated_template("header.hpp.in", mock_registry_h, {})
    add_generated_template("header.hpp.in", mock_impl_h, {})
    if kind == "textual" then
        add_generated_template("textual_mock_defs.cpp.in", defs_cpp, {
            DEFS_INCLUDES = include_lines(defs_cpp, defs),
            HEADER_FILENAME = path.filename(codegen_h),
        })
        add_generated_template("textual_mock_public.hpp.in", public_header, {
            DEFS_INCLUDES = include_lines(public_header, defs),
            MOCK_REGISTRY_FILENAME = path.filename(mock_registry_h),
            MOCK_IMPL_FILENAME = path.filename(mock_impl_h),
        })
    else
        add_generated_template("module_public.cppm.in", public_module, {
            MODULE_NAME = config.module_name,
            EXPORTED_IMPORTS = export_import_lines(config.defs_modules),
        })
    end
    on_load(function (target)
        if kind == "modules" then
            for index, defs_file in ipairs(defs) do
                local source_abs = project_path(defs_file)
                local source_body = io.readfile(source_abs)
                if not source_body then
                    fail("gentest_add_mocks(kind='modules') could not read `" .. tostring(defs_file) .. "`")
                end

                if not source_body:find("import%s+gentest%.mock%s*;") then
                    local _, decl_end = source_body:find("export%s+module%s+[^;]+;")
                    if not decl_end then
                        _, decl_end = source_body:find("module%s+[^;]+;")
                    end
                    if not decl_end then
                        fail("gentest_add_mocks(kind='modules') requires a named module declaration in `" .. tostring(defs_file) .. "`")
                    end
                    source_body = source_body:sub(1, decl_end) .. "\n\nimport gentest.mock;" .. source_body:sub(decl_end + 1)
                end

                local wrapper_output = config.module_wrapper_outputs[index]
                local header_output = config.module_header_outputs[index]
                local wrapper_dir = path.directory(wrapper_output)
                if wrapper_dir and wrapper_dir ~= "" then
                    os.mkdir(wrapper_dir)
                end
                io.writefile(wrapper_output, source_body)
                io.writefile(header_output, "")
            end
        end
        local dep_include_dirs = resolve_dep_inputs(config.deps)
        for _, include_dir in ipairs(dep_include_dirs) do
            target:add("includedirs", include_dir)
            append_unique(include_dirs, seen_registered_includes, include_dir)
        end
    end)
    before_buildcmd(function (target, batchcmds)
        if kind == "modules" then
            require_clang_module_toolchain(target, "gentest_add_mocks")
        end
        local codegen, compdb_dir, host_clang, scan_deps = ensure_codegen(batchcmds, target)
        config.extra_includes = collect_target_package_include_dirs(target)
        local dep_include_dirs = resolve_dep_inputs(config.deps)
        local dep_metadata_include_dirs, dep_module_sources = collect_mock_metadata_inputs(config.deps)
        local seen_build_includes = {}
        for _, include_dir in ipairs(config.extra_includes) do
            seen_build_includes[include_dir] = true
        end
        for _, include_dir in ipairs(dep_include_dirs) do
            if not seen_build_includes[include_dir] then
                seen_build_includes[include_dir] = true
                table.insert(config.extra_includes, include_dir)
            end
        end
        for _, include_dir in ipairs(dep_metadata_include_dirs) do
            if not seen_build_includes[include_dir] then
                seen_build_includes[include_dir] = true
                table.insert(config.extra_includes, include_dir)
            end
        end
        config.dep_module_sources = dep_module_sources
        run_mock_codegen(batchcmds, codegen, compdb_dir, host_clang, scan_deps, config)
    end)

    registered_target_metadata()[target_name] = {
        target = target_name,
        include_dir = out_dir_abs,
        include_dirs = include_dirs,
        deps = opts.deps or {},
        mock_metadata = kind == "textual" and textual_mock_metadata_payload(config) or module_mock_metadata_payload(config),
    }
end

function gentest_attach_codegen(opts)
    local kind = require_kind(opts, "gentest_attach_codegen")
    if kind == "modules" then
        require_clang_module_toolchain(nil, "gentest_attach_codegen")
    end

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
        wrapper_cpp = module_wrapper_output_rel(output_dir, source, 0)
        wrapper_h = module_header_output_rel(output_dir, source, 0)
    end
    local wrapper_d = path.join(output_dir, basename_stem(wrapper_h) .. ".d")
    local extra_includes = {}
    local seen_extra_includes = {}
    for _, include_dir in ipairs(opts.includes or {}) do
        append_unique(extra_includes, seen_extra_includes, include_dir)
    end
    local dep_targets = collect_dep_targets(opts.deps)
    local config = {
        kind = kind,
        out_dir_abs = out_dir_abs,
        wrapper_output = project_path(wrapper_cpp),
        header_output = project_path(wrapper_h),
        depfile = project_path(wrapper_d),
        source_file = project_path(source),
        extra_includes = extra_includes,
        dep_module_sources = {},
        deps = opts.deps or {},
        defines = opts.defines or {},
        clang_args = opts.clang_args or {},
    }
    set_configdir(project_root())
    add_packages("fmt")
    add_includedirs(incdirs())
    add_defines(gentest_common_defines())
    if opts.defines and #opts.defines > 0 then
        add_defines(table.unpack(opts.defines))
    end
    add_cxxflags(table.unpack(gentest_common_cxxflags()), {force = true})
    if opts.clang_args and #opts.clang_args > 0 then
        add_cxxflags(table.unpack(opts.clang_args), {force = true})
    end
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
    if kind == "textual" then
        add_generated_template("suite_wrapper.cpp.in", wrapper_cpp, {
            SOURCE_INCLUDE = relative_include(wrapper_cpp, source),
            HEADER_FILENAME = path.filename(wrapper_h),
        })
    else
        add_generated_template("header.hpp.in", wrapper_h, {})
        add_generated_copy(source, wrapper_cpp)
    end
    on_load(function (target)
        local dep_include_dirs = resolve_dep_inputs(config.deps)
        for _, include_dir in ipairs(extra_includes) do
            target:add("includedirs", include_dir)
        end
        for _, include_dir in ipairs(dep_include_dirs) do
            append_unique(extra_includes, seen_extra_includes, include_dir)
            target:add("includedirs", include_dir)
        end
    end)
    before_buildcmd(function (target, batchcmds)
        if kind == "modules" then
            require_clang_module_toolchain(target, "gentest_attach_codegen")
        end
        local codegen, compdb_dir, host_clang, scan_deps = ensure_codegen(batchcmds, target)
        local dep_include_dirs = resolve_dep_inputs(config.deps)
        local dep_metadata_include_dirs, dep_module_sources = collect_mock_metadata_inputs(config.deps)
        for _, include_dir in ipairs(dep_include_dirs) do
            append_unique(config.extra_includes, seen_extra_includes, include_dir)
        end
        for _, include_dir in ipairs(dep_metadata_include_dirs) do
            append_unique(config.extra_includes, seen_extra_includes, include_dir)
        end
        local package_include_dirs = collect_target_package_include_dirs(target)
        for _, include_dir in ipairs(package_include_dirs) do
            append_unique(config.extra_includes, seen_extra_includes, include_dir)
        end
        config.dep_module_sources = dep_module_sources
        run_suite_codegen(batchcmds, codegen, compdb_dir, host_clang, scan_deps, config)
    end)
end
