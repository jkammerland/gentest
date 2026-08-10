local gentest_state = {}
local fail

fail = function(message)
    error("gentest xmake: " .. message, 0)
end

-- Configure the shared Xmake helper context. External consumers can override
-- codegen_project_root to point at a gentest checkout, or provide
-- codegen = { exe = ..., clang = ..., scan_deps = ..., parse_cache = ...,
--             parse_cache_dir = ..., compiler_cache = "off"|"xmake" }.
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

local codegen_project_root
local project_path

local function helper_script_dir()
    local configured = gentest_state["helper_root"]
    if configured ~= nil and tostring(configured) ~= "" then
        local helper_dir = tostring(configured)
        if not path.is_absolute(helper_dir) then
            helper_dir = project_path(helper_dir)
        end
        return path.absolute(helper_dir)
    end
    local script_dir = os.scriptdir()
    if script_dir and script_dir ~= "" then
        return path.absolute(script_dir)
    end
    return path.join(project_root(), "xmake")
end

local function incdirs()
    return state_value("incdirs")
end

project_path = function(filepath)
    if path.is_absolute(filepath) then
        return filepath
    end
    return path.join(project_root(), filepath)
end

local function normalize_root_candidate(candidate)
    local candidate_text = tostring(candidate or "")
    if candidate_text == "" then
        return nil
    end
    if not path.is_absolute(candidate_text) then
        candidate_text = project_path(candidate_text)
    end
    local normalized = path.absolute(candidate_text)
    if os.isfile(path.join(normalized, "include", "gentest", "runner.h")) then
        return normalized
    end
    if os.isfile(path.join(normalized, "include", "gentest", "gentest.cppm")) then
        return normalized
    end
    return nil
end

local function gentest_root()
    local configured = gentest_state["gentest_root"]
    if configured ~= nil and tostring(configured) ~= "" then
        local resolved = normalize_root_candidate(configured)
        if resolved then
            return resolved
        end
        fail("gentest_configure `gentest_root` must point at a gentest source tree or installed prefix")
    end

    local script_dir = helper_script_dir()
    local candidates = {
        path.directory(script_dir),
        path.directory(path.directory(path.directory(script_dir))),
        codegen_project_root(),
        project_root(),
    }
    for _, candidate in ipairs(candidates) do
        local resolved = normalize_root_candidate(candidate)
        if resolved then
            return resolved
        end
    end
    fail("failed to resolve gentest_root; set gentest_configure({ gentest_root = ... })")
end

local function configured_build_dir()
    local builddir = get_config("builddir") or get_config("buildir") or "build"
    return project_path(builddir)
end

local function resolved_incdirs()
    local result = {}
    local seen = {}
    local gentest_include = path.join(gentest_root(), "include")
    if os.isdir(gentest_include) then
        table.insert(result, gentest_include)
        seen[gentest_include] = true
    end
    for _, include_dir in ipairs(incdirs()) do
        local resolved = include_dir
        if path.is_absolute(include_dir) then
            resolved = include_dir
        else
            resolved = project_path(include_dir)
        end
        if not seen[resolved] then
            seen[resolved] = true
            table.insert(result, resolved)
        end
    end
    return result
end

codegen_project_root = function()
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
            local resolved = os.readlink(link_path)
            if resolved and resolved ~= "" then
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

local function gentest_module_files()
    local configured = gentest_state["gentest_module_files"]
    if configured == nil then
        return {}
    end
    if type(configured) ~= "table" then
        fail("gentest_configure `gentest_module_files` must be a table when provided")
    end
    local result = {}
    for _, filepath in ipairs(configured) do
        local filepath_text = tostring(filepath or "")
        if filepath_text ~= "" then
            if path.is_absolute(filepath_text) then
                table.insert(result, filepath_text)
            else
                table.insert(result, path.join(gentest_root(), filepath_text))
            end
        end
    end
    return result
end

local function materialized_public_module_entries(output_dir)
    local entries = {}
    for _, module_source in ipairs(gentest_module_files()) do
        local output_rel = path.join(output_dir, "__gentest_public_modules", path.filename(module_source))
        table.insert(entries, {
            source = module_source,
            output_rel = output_rel,
            output_abs = project_path(output_rel),
        })
    end
    return entries
end

local function gentest_public_include_dir()
    return path.join(gentest_root(), "include")
end

local function gentest_public_linkdirs()
    local result = {}
    for _, dir_name in ipairs({"lib", "lib64"}) do
        local candidate = path.join(gentest_root(), dir_name)
        if os.isdir(candidate) then
            table.insert(result, candidate)
        end
    end
    return result
end

local function current_mode_name()
    local mode = tostring(get_config("mode") or ""):lower()
    if mode == "" then
        return "release"
    end
    return mode
end

local function detect_installed_library_name(debug_name, release_name)
    local candidates = {release_name, debug_name}
    if current_mode_name() == "debug" then
        candidates = {debug_name, release_name}
    end
    for _, linkdir in ipairs(gentest_public_linkdirs()) do
        for _, candidate in ipairs(candidates) do
            local matches = os.files(path.join(linkdir, "*" .. candidate .. "*"))
            if matches and #matches > 0 then
                return candidate
            end
        end
    end
    return nil
end

local function gentest_runtime_link_name()
    return detect_installed_library_name("gentest_runtimed", "gentest_runtime")
end

local function gentest_module_link_name()
    return detect_installed_library_name("gentestd", "gentest")
end

local function gentest_fmt_link_name()
    return detect_installed_library_name("fmtd", "fmt")
end

local function gentest_uses_installed_libraries()
    return #gentest_public_linkdirs() > 0
end

local function default_windows_llvm_contract()
    return {
        runtime = current_mode_name() == "debug" and "MTd" or "MT",
        defines = {"FMT_USE_CONSTEVAL=0", "_ITERATOR_DEBUG_LEVEL=0", "_HAS_ITERATOR_DEBUGGING=0"},
    }
end

local function resolved_windows_llvm_contract()
    local runtime = os.getenv("GENTEST_XMAKE_WINDOWS_RUNTIME")
    local defines = {}
    local env_defines = os.getenv("GENTEST_XMAKE_WINDOWS_DEFINES")
    if env_defines and env_defines ~= "" then
        for define in env_defines:gmatch("[^;]+") do
            if define ~= "" then
                table.insert(defines, define)
            end
        end
    end

    local default_contract = default_windows_llvm_contract()
    if (runtime and runtime ~= "") or #defines > 0 then
        return {
            runtime = runtime,
            defines = defines,
        }
    end

    return {
        runtime = default_contract.runtime,
        defines = default_contract.defines,
    }
end

local function registered_target_metadata()
    local metadata = gentest_state["registered_target_metadata"]
    if metadata == nil then
        metadata = {}
        gentest_state["registered_target_metadata"] = metadata
    end
    return metadata
end

local function invalidate_xmake_module_scanner_cache(generated_target, project, localcache)
    local generated_fullname = generated_target:fullname()
    local function depends_on_generated(candidate, seen)
        local candidate_fullname = candidate:fullname()
        if candidate_fullname == generated_fullname then
            return true
        end
        if seen[candidate_fullname] then
            return false
        end
        seen[candidate_fullname] = true
        for _, dep in pairs(candidate:deps() or {}) do
            if depends_on_generated(dep, seen) then
                return true
            end
        end
        return false
    end

    local cache = localcache.cache("cxxmodules")
    for _, candidate in pairs(project.targets()) do
        if depends_on_generated(candidate, {}) then
            local affected_fullname = candidate:fullname()
            cache:set2(affected_fullname, "sourcebatch_sum", nil)
            cache:set2(affected_fullname, "c++.build.sourcebatch", nil)
            cache:set2(affected_fullname, "c++.modules", nil)
            cache:set2(affected_fullname, "c++.modules.built_artifacts", nil)
        end
    end
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

local function append_common_codegen_driver_args(args, extra_include_dirs, extra_defines, extra_clang_args, forced_includes)
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
    for _, forced_include in ipairs(forced_includes or {}) do
        table.insert(args, "-include")
        table.insert(args, forced_include)
    end
end

local function default_external_module_sources()
    return {
        "gentest=" .. path.join(gentest_root(), "include", "gentest", "gentest.cppm"),
        "gentest.mock=" .. path.join(gentest_root(), "include", "gentest", "gentest.mock.cppm"),
        "gentest.bench_util=" .. path.join(gentest_root(), "include", "gentest", "gentest.bench_util.cppm"),
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

local function module_registration_output_rel(output_dir, source, index)
    return path.join(
        output_dir,
        string.format("tu_%04d_%s.registration.gentest.cpp", index, shorten_generated_stem(basename_stem(source)))
    )
end

local function mock_domain_output_rel(input_path, index, label)
    local domain_dir = path.directory(input_path)
    local domain_stem = path.basename(input_path):gsub("%.[^.]+$", "")
    local domain_ext = path.extension(input_path)
    local domain_label = tostring(label or ""):gsub("[^%w_]", "_")
    if domain_label == "" then
        domain_label = "domain"
    end
    if domain_label ~= "header" and #domain_label > 32 then
        domain_label = domain_label:sub(1, 16) .. "_" .. shorten_stem_digest(domain_label):sub(1, 8)
    end
    return path.join(domain_dir, string.format("%s__domain_%04d_%s%s", domain_stem, index, domain_label, domain_ext))
end

local function module_header_output_rel(output_dir, source, index)
    return path.join(output_dir, string.format("tu_%04d_%s.gentest.h", index, shorten_generated_stem(basename_stem(source))))
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

local function normalize_opt_in(value, label)
    if value == nil or value == "" then
        return nil
    end
    if value == true or value == 1 then
        return true
    end
    if value == false or value == 0 then
        return false
    end
    local normalized = tostring(value):lower()
    if normalized == "on" or normalized == "true" or normalized == "yes" or normalized == "y" or normalized == "1" then
        return true
    end
    if normalized == "off" or normalized == "false" or normalized == "no" or normalized == "n" or normalized == "0" then
        return false
    end
    fail(label .. " must be ON or OFF")
end

local function resolved_parse_cache_dir()
    local codegen = configured_codegen_settings()
    if not normalize_opt_in(codegen["parse_cache"], "gentest_configure().codegen.parse_cache") then
        return nil
    end
    local configured = tostring(codegen["parse_cache_dir"] or "")
    if configured == "" then
        return path.absolute(path.join(configured_build_dir(), ".gentest_codegen_parse_cache"))
    end
    if not path.is_absolute(configured) then
        configured = path.join(configured_build_dir(), configured)
    end
    return path.absolute(configured)
end

local function append_parse_cache_args(args)
    local cache_dir = resolved_parse_cache_dir()
    if cache_dir then
        table.insert(args, "--parse-cache-dir")
        table.insert(args, cache_dir)
    end
end

local function compiler_cache_policy()
    local codegen = configured_codegen_settings()
    local configured = codegen["compiler_cache"]
    if configured == nil or configured == "" then
        configured = os.getenv("GENTEST_XMAKE_COMPILER_CACHE") or "off"
    end
    local policy = tostring(configured):lower()
    if policy ~= "off" and policy ~= "xmake" then
        fail("gentest_configure().codegen.compiler_cache must be `off` or `xmake`")
    end
    return policy
end

-- Apply Gentest's compiler-cache policy to the current target. This is
-- intentionally target-scoped: unrelated user targets retain Xmake's own
-- policy/default rather than inheriting a project-global setting.
function gentest_apply_compiler_cache_policy(kind)
    local policy = compiler_cache_policy()
    -- This target-scope DSL call writes the target's actual policy table.
    -- It deliberately does not use an `after_load` hook: a helper target may
    -- already have an after_load callback for module-scanner invalidation.
    -- Xmake's module rule may also force this policy off while loading.
    set_policy("build.ccache", kind ~= "modules" and policy == "xmake")
    if kind ~= "modules" and policy == "xmake" then
        -- Keep the opt-in cache inside this build tree. Do not opt targets
        -- into Xmake's user-global cache storage.
        set_policy("build.ccache.global_storage", false)
    end
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

local function ensure_materialized_public_modules(entries, runtime_os)
    for _, entry in ipairs(entries or {}) do
        local output_dir = path.directory(entry.output_abs)
        if output_dir and output_dir ~= "" then
            runtime_os.mkdir(output_dir)
        end
        runtime_os.cp(entry.source, entry.output_abs)
    end
end

local function ensure_fallback_compdb(filepath, runtime_os, runtime_io)
    local output_dir = path.directory(filepath)
    if output_dir and output_dir ~= "" then
        runtime_os.mkdir(output_dir)
    end
    if not runtime_os.isfile(filepath) or runtime_io.readfile(filepath) ~= "[]\n" then
        runtime_io.writefile(filepath, "[]\n")
    end
end

local function ensure_textual_mock_aggregate(filepath, defs, runtime_os, runtime_io)
    local output_dir = path.directory(filepath)
    if output_dir and output_dir ~= "" then
        runtime_os.mkdir(output_dir)
    end

    local lines = {"// This file is auto-generated by gentest's Xmake helper.", "// Do not edit manually.", ""}
    for _, defs_file in ipairs(defs) do
        local include_path = project_path(defs_file):gsub("\\", "/")
        table.insert(lines, "#include \"" .. include_path .. "\"")
    end
    table.insert(lines, "")
    local contents = table.concat(lines, "\n")
    if not runtime_os.isfile(filepath) or runtime_io.readfile(filepath) ~= contents then
        runtime_io.writefile(filepath, contents)
    end
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

local function configured_toolchain_hint()
    local configured = get_config("toolchain")
    if type(configured) == "string" then
        return configured
    end
    return ""
end

function gentest_apply_windows_llvm_toolchain()
    if not is_host("windows") then
        return
    end
    local configured_toolchain = configured_toolchain_hint():lower()
    local contract = resolved_windows_llvm_contract()
    if configured_toolchain == "llvm" then
        set_toolchains("llvm")
        if contract.runtime and contract.runtime ~= "" then
            set_runtimes(contract.runtime)
        end
        if contract.defines and #contract.defines > 0 then
            add_defines(table.unpack(contract.defines))
        end
        return
    end
    local cxx_tool = configured_cxx_tool_hint()
    if cxx_tool ~= "" and is_clang_tool(cxx_tool) then
        set_toolchains("llvm")
        if contract.runtime and contract.runtime ~= "" then
            set_runtimes(contract.runtime)
        end
        if contract.defines and #contract.defines > 0 then
            add_defines(table.unpack(contract.defines))
        end
    end
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
    for _, environment_name in ipairs({"CXX", "CC"}) do
        local environment_tool = os.getenv(environment_name) or ""
        if environment_tool ~= "" and is_clang_tool(environment_tool) then
            local resolved = resolve_program_candidate(environment_tool)
            if resolved then
                return resolved
            end
        end
    end
    local candidates = is_host("windows") and {
        "clang++.exe", "clang.exe", "clang++", "clang",
    } or {
        "clang++", "clang", "clang++-23", "clang++-22", "clang++-21", "clang++-20", "clang-23", "clang-22",
        "clang-21", "clang-20",
    }
    for _, candidate in ipairs(candidates) do
        local resolved = resolve_program_candidate(candidate)
        if resolved then
            return resolved
        end
    end
    fail(
        "Gentest code generation requires a resolvable host Clang executable. Configure "
            .. "gentest_configure({ codegen = { clang = ... }}) or GENTEST_CODEGEN_HOST_CLANG."
    )
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
    if not batchcmds then
        fail("code generation commands must be scheduled through an Xmake build rule")
    end
    batchcmds:vrunv(program, args)
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

local function fallback_compdb_paths(output_dir)
    local compdb_dir = project_path(path.join(output_dir, ".gentest_compdb"))
    return compdb_dir, path.join(compdb_dir, "compile_commands.json")
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

local function mock_codegen_args(compdb_dir, host_clang, scan_deps, config)
    local args = {
        "--source-root", project_root(),
        "--tu-out-dir", config.out_dir_abs,
        "--mock-registry", config.mock_registry,
        "--mock-impl", config.mock_impl,
        "--discover-mocks",
    }
    if config.depfile and config.depfile ~= "" then
        table.insert(args, "--depfile")
        table.insert(args, config.depfile)
    end
    if config.lookup_guard_output and config.lookup_guard_output ~= "" then
        table.insert(args, "--lookup-guard-output")
        table.insert(args, config.lookup_guard_output)
    end
    for _, domain_output in ipairs(config.mock_domain_registry_outputs or {}) do
        table.insert(args, "--mock-domain-registry-output")
        table.insert(args, domain_output)
    end
    for _, domain_output in ipairs(config.mock_domain_impl_outputs or {}) do
        table.insert(args, "--mock-domain-impl-output")
        table.insert(args, domain_output)
    end
    if config.kind == "textual" then
        table.insert(args, "--tu-header-output")
        table.insert(args, config.header_output)
        table.insert(args, "--textual-wrapper-output")
        table.insert(args, config.wrapper_output)
        table.insert(args, "--mock-public-header")
        table.insert(args, config.public_header)
        table.insert(args, config.source_file)
    else
        for _, header_output in ipairs(config.module_header_outputs or {}) do
            table.insert(args, "--tu-header-output")
            table.insert(args, header_output)
        end
        for _, wrapper_output in ipairs(config.module_wrapper_outputs or {}) do
            table.insert(args, "--module-wrapper-output")
            table.insert(args, wrapper_output)
        end
        for _, module_source in ipairs(default_external_module_sources()) do
            table.insert(args, "--external-module-source")
            table.insert(args, module_source)
        end
        for _, module_source in ipairs(config.dep_module_sources or {}) do
            table.insert(args, "--external-module-source")
            table.insert(args, module_source)
        end
        table.insert(args, "--mock-aggregate-module-output")
        table.insert(args, config.public_module)
        table.insert(args, "--mock-aggregate-module-name")
        table.insert(args, config.module_name)
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
    if compdb_dir then
        table.insert(args, "--compdb")
        table.insert(args, compdb_dir)
    end
    append_parse_cache_args(args)
    append_common_codegen_driver_args(args, config.extra_includes, config.defines, config.clang_args, config.forced_includes)
    return args
end

local function suite_codegen_args(compdb_dir, host_clang, scan_deps, config)
    local args = {
        "--source-root", project_root(),
        "--tu-out-dir", config.out_dir_abs,
        "--tu-header-output", config.header_output,
    }
    if config.depfile and config.depfile ~= "" then
        table.insert(args, "--depfile")
        table.insert(args, config.depfile)
    end
    if config.lookup_guard_output and config.lookup_guard_output ~= "" then
        table.insert(args, "--lookup-guard-output")
        table.insert(args, config.lookup_guard_output)
    end
    if compdb_dir then
        table.insert(args, "--compdb")
        table.insert(args, compdb_dir)
    end
    if config.kind == "modules" then
        table.insert(args, "--module-registration-output")
        table.insert(args, config.registration_output)
        table.insert(args, "--artifact-manifest")
        table.insert(args, config.artifact_manifest)
        table.insert(args, "--compile-context-id")
        table.insert(args, config.compile_context_id)
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
        table.insert(args, "--artifact-manifest")
        table.insert(args, config.artifact_manifest)
        table.insert(args, "--textual-wrapper-output")
        table.insert(args, config.wrapper_output)
        table.insert(args, "--artifact-owner-source")
        table.insert(args, config.source_file)
        table.insert(args, "--compile-context-id")
        table.insert(args, config.compile_context_id)
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
    append_parse_cache_args(args)
    append_common_codegen_driver_args(args, config.extra_includes, config.defines, config.clang_args)
    return args
end

local function append_path_unique(paths, seen, filepath)
    if filepath == nil or filepath == "" then
        return
    end
    local normalized = tostring(filepath)
    if not path.is_absolute(normalized) then
        normalized = project_path(normalized)
    end
    normalized = path.absolute(normalized)
    if not seen[normalized] then
        seen[normalized] = true
        table.insert(paths, normalized)
    end
end

local function append_external_module_source_path(paths, seen, mapping)
    -- `--external-module-source` carries `module-name=/path/to/source`.
    -- These source units are recorded in the artifact manifest rather than in
    -- the codegen depfile, so their RHS must be a static dependency as well.
    local text = tostring(mapping or "")
    local separator = text:find("=", 1, true)
    if separator and separator < #text then
        append_path_unique(paths, seen, text:sub(separator + 1))
    end
end

local function value_identity(value)
    if type(value) ~= "table" then
        return tostring(value or "")
    end
    local values = {}
    for _, item in ipairs(value) do
        table.insert(values, value_identity(item))
    end
    return "[" .. table.concat(values, "\31") .. "]"
end

local codegen_environment_identity_names = {
    "GENTEST_CODEGEN_RESOURCE_DIR",
    "GENTEST_CODEGEN_SCAN_DEPS_MODE",
    "GENTEST_CODEGEN_PARSE_CACHE",
    "GENTEST_CODEGEN_PARSE_CACHE_DIR",
    "GENTEST_CODEGEN_PARSE_CACHE_SALT",
    "GENTEST_CODEGEN_PCM_CACHE",
    "GENTEST_CODEGEN_PCM_CACHE_DIR",
    "GENTEST_CODEGEN_PCM_CACHE_SALT",
    "GENTEST_STRICT_FIXTURE",
    "GENTEST_NO_INCLUDE_SOURCES",
    "CPATH",
    "C_INCLUDE_PATH",
    "CPLUS_INCLUDE_PATH",
    "OBJC_INCLUDE_PATH",
    "OBJCPLUS_INCLUDE_PATH",
    "INCLUDE",
    "SDKROOT",
}

local function codegen_environment_identity()
    local values = {}
    for _, name in ipairs(codegen_environment_identity_names) do
        table.insert(values, name .. "=" .. tostring(os.getenv(name) or ""))
    end
    return table.concat(values, "\31")
end

local function target_codegen_identity(target, config, codegen, compdb_dir, host_clang, scan_deps)
    local cxx_program, cxx_name = target:tool("cxx")
    local target_values = {
        "gentest-xmake-codegen-v3",
        config.operation or "",
        config.codegen_kind or "",
        config.kind,
        config.source_file or "",
        config.module_name or "",
        codegen or "",
        compdb_dir or "",
        host_clang or "",
        scan_deps or "",
        gentest_root(),
        helper_script_dir(),
        value_identity(resolved_incdirs()),
        resolved_parse_cache_dir() or "",
        tostring(configured_codegen_settings()["parse_cache"] or ""),
        codegen_environment_identity(),
        value_identity(config.inputs),
        value_identity(config.defines),
        value_identity(config.clang_args),
        value_identity(config.forced_includes),
        value_identity(config.extra_includes),
        value_identity(config.dep_module_sources),
        value_identity(config.dep_support_headers),
        value_identity(config.outputs),
        value_identity(gentest_common_defines()),
        value_identity(gentest_common_cxxflags()),
        value_identity(target:get("defines")),
        value_identity(target:get("cxflags")),
        value_identity(target:get("cxxflags")),
        cxx_program or "",
        cxx_name or "",
    }
    return table.concat(target_values, "\30")
end

local function prepare_mock_codegen_inputs(target, config)
    config.extra_includes = collect_target_package_include_dirs(target)
    local dep_include_dirs = resolve_dep_inputs(config.deps)
    local dep_metadata_include_dirs, dep_module_sources, support_headers = collect_mock_metadata_inputs(config.deps)
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
    config.dep_support_headers = support_headers
end

local function prepare_suite_codegen_inputs(target, config)
    local seen_extra_includes = {}
    for _, include_dir in ipairs(config.extra_includes) do
        seen_extra_includes[include_dir] = true
    end
    local dep_include_dirs = resolve_dep_inputs(config.deps)
    local dep_metadata_include_dirs, dep_module_sources, support_headers = collect_mock_metadata_inputs(config.deps)
    for _, include_dir in ipairs(dep_include_dirs) do
        append_unique(config.extra_includes, seen_extra_includes, include_dir)
    end
    for _, include_dir in ipairs(dep_metadata_include_dirs) do
        append_unique(config.extra_includes, seen_extra_includes, include_dir)
    end
    for _, include_dir in ipairs(collect_target_package_include_dirs(target)) do
        append_unique(config.extra_includes, seen_extra_includes, include_dir)
    end
    config.dep_module_sources = dep_module_sources
    config.dep_support_headers = support_headers
end

local function codegen_static_dependencies(target, config, codegen, compdb_dir, host_clang, scan_deps)
    local files = {}
    local seen = {}
    for _, input in ipairs(config.inputs or {}) do
        append_path_unique(files, seen, input)
    end
    for _, output in ipairs(config.outputs or {}) do
        append_path_unique(files, seen, output)
    end
    for _, support_header in ipairs(config.dep_support_headers or {}) do
        append_path_unique(files, seen, support_header)
    end
    if config.kind == "modules" then
        for _, module_source in ipairs(default_external_module_sources()) do
            append_external_module_source_path(files, seen, module_source)
        end
        for _, module_source in ipairs(config.dep_module_sources or {}) do
            append_external_module_source_path(files, seen, module_source)
        end
    end
    append_path_unique(files, seen, codegen)
    append_path_unique(files, seen, host_clang)
    append_path_unique(files, seen, scan_deps)
    append_path_unique(files, seen, path.join(helper_script_dir(), "gentest.lua"))
    append_path_unique(files, seen, path.join(helper_script_dir(), "scripts", "update_codegen_dep_cache.lua"))
    append_path_unique(files, seen, path.join(helper_script_dir(), "scripts", "codegen_dep_cache_common.lua"))
    append_path_unique(files, seen, path.join(helper_script_dir(), "scripts", "run_codegen_with_dep_cache.lua"))
    append_path_unique(files, seen, os.programfile())
    local cxx_program = target:tool("cxx")
    append_path_unique(files, seen, cxx_program)
    if compdb_dir and compdb_dir ~= "" then
        append_path_unique(files, seen, path.join(compdb_dir, "compile_commands.json"))
    end
    return files
end

local function generation_cache_path(config)
    return config.depcache_anchor .. ".gentest_codegen_deps"
end

local function run_cached_codegen(target, batchcmds, config)
    if config.kind == "modules" then
        require_clang_module_toolchain(target, config.operation)
    end
    config.prepare_inputs(target, config)
    local codegen, compdb_dir, host_clang, scan_deps = ensure_codegen(batchcmds, target)
    if not compdb_dir then
        compdb_dir = config.fallback_compdb_dir
    end
    local static_dependencies = codegen_static_dependencies(target, config, codegen, compdb_dir, host_clang, scan_deps)
    local identity = target_codegen_identity(target, config, codegen, compdb_dir, host_clang, scan_deps)
    local cache_path = generation_cache_path(config)
    local codegen_args = nil
    if config.codegen_kind == "mocks" then
        codegen_args = mock_codegen_args(compdb_dir, host_clang, scan_deps, config)
    else
        codegen_args = suite_codegen_args(compdb_dir, host_clang, scan_deps, config)
    end
    local cache_args = {
        cache_path,
        config.depfile,
        config.lookup_guard_output or "",
        identity,
        project_root(),
        tostring(#static_dependencies),
        codegen,
    }
    for _, filepath in ipairs(static_dependencies) do
        table.insert(cache_args, filepath)
    end
    for _, argument in ipairs(codegen_args) do
        table.insert(cache_args, argument)
    end
    local runner_args = {"lua", path.join(helper_script_dir(), "scripts", "run_codegen_with_dep_cache.lua")}
    for _, argument in ipairs(cache_args) do
        table.insert(runner_args, argument)
    end
    -- Run the cache owner as a real child process. Xmake's in-process `vlua`
    -- helper logs script failures without reliably failing the enclosing
    -- build, which would hide codegen validation errors.
    batchcmds:vrunv(os.programfile(), runner_args)
end

local function generation_configs()
    local configs = gentest_state["_generation_configs"]
    if not configs then
        configs = {}
        gentest_state["_generation_configs"] = configs
    end
    return configs
end

local function register_generation_config(target_name, config)
    generation_configs()[target_name] = config
end

rule("gentest.codegen.prepare")
    on_preparecmd_file(function(target, batchcmds, sourcefile, opt)
        -- Xmake strips the namespace from target:name(); helper callers use
        -- the public full target name in opts.name, so preserve it here.
        local target_name = target:fullname()
        local config = generation_configs()[target_name] or generation_configs()[target:name()]
        if not config then
            fail("missing codegen configuration for target `" .. target_name .. "`")
        end
        run_cached_codegen(target, batchcmds, config)
    end)

function gentest_add_mocks(opts)
    local kind = require_kind(opts, "gentest_add_mocks")
    if kind == "modules" then
        require_clang_module_toolchain(nil, "gentest_add_mocks")
    end

    gentest_apply_windows_llvm_toolchain()
    gentest_apply_compiler_cache_policy(kind)

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
    local mock_registry_h = path.join(output_dir, target_id .. "_mock_registry.hpp")
    local mock_impl_h = path.join(output_dir, target_id .. "_mock_impl.hpp")
    local mock_depfile = path.join(output_dir, target_id .. "_mock_codegen.d")
    local fallback_compdb_dir, fallback_compdb_file = fallback_compdb_paths(output_dir)
    local config = {
        kind = kind,
        operation = "gentest_add_mocks",
        codegen_kind = "mocks",
        defs = {},
        out_dir_abs = out_dir_abs,
        fallback_compdb_dir = fallback_compdb_dir,
        fallback_compdb_file = fallback_compdb_file,
        mock_registry = project_path(mock_registry_h),
        mock_impl = project_path(mock_impl_h),
        depfile = project_path(mock_depfile),
        mock_domain_registry_outputs = {project_path(mock_domain_output_rel(mock_registry_h, 0, "header"))},
        mock_domain_impl_outputs = {project_path(mock_domain_output_rel(mock_impl_h, 0, "header"))},
        target_id = target_id,
        extra_includes = {},
        dep_module_sources = {},
        deps = opts.deps or {},
        defines = opts.defines or {},
        clang_args = opts.clang_args or {},
        forced_includes = {},
        defs_modules = defs_modules or {},
        public_modules_via_deps = opts.public_modules_via_deps == true,
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
        local defs_input = path.join(output_dir, target_id .. "_defs_input.cpp")
        codegen_h = path.join(output_dir, "tu_0000_" .. target_id .. "_defs.gentest.h")
        public_header = path.join(output_dir, public_header_name)
        config.wrapper_output = project_path(defs_cpp)
        config.header_output = project_path(codegen_h)
        config.public_header = project_path(public_header)
        config.aggregate_source = project_path(defs_input)
        config.source_file = config.aggregate_source
        table.insert(config.forced_includes, "gentest/mock.h")
        add_private_files = {defs_cpp}
    else
        local module_name = require_opt(opts, "module_name", "gentest_add_mocks")
        config.module_name = module_name
        config.module_wrapper_outputs = {}
        config.module_header_outputs = {}
        local seen_mock_modules = {}
        local mock_domain_index = 1
        if not config.public_modules_via_deps then
            config.public_module_entries = materialized_public_module_entries(output_dir)
        else
            config.public_module_entries = {}
        end
        for index, defs_file in ipairs(defs) do
            local zero_index = index - 1
            local wrapper_rel = module_wrapper_output_rel(output_dir, defs_file, zero_index)
            local header_rel = module_header_output_rel(output_dir, defs_file, zero_index)
            table.insert(config.module_wrapper_outputs, project_path(wrapper_rel))
            table.insert(config.module_header_outputs, project_path(header_rel))
            table.insert(add_public_files, wrapper_rel)
            local defs_module = defs_modules[index]
            if defs_module and not seen_mock_modules[defs_module] then
                seen_mock_modules[defs_module] = true
                table.insert(
                    config.mock_domain_registry_outputs,
                    project_path(mock_domain_output_rel(mock_registry_h, mock_domain_index, defs_module))
                )
                table.insert(
                    config.mock_domain_impl_outputs,
                    project_path(mock_domain_output_rel(mock_impl_h, mock_domain_index, defs_module))
                )
                mock_domain_index = mock_domain_index + 1
            end
        end
        public_module = module_public_output_rel(output_dir, module_name)
        config.public_module = project_path(public_module)
    end
    for _, defs_file in ipairs(defs) do
        table.insert(config.defs, project_path(defs_file))
    end
    config.inputs = {}
    for _, defs_file in ipairs(config.defs) do
        table.insert(config.inputs, defs_file)
    end
    if config.aggregate_source then
        table.insert(config.inputs, config.aggregate_source)
    end
    config.outputs = {
        config.depfile,
        config.mock_registry,
        config.mock_impl,
    }
    for _, output in ipairs(config.mock_domain_registry_outputs) do
        table.insert(config.outputs, output)
    end
    for _, output in ipairs(config.mock_domain_impl_outputs) do
        table.insert(config.outputs, output)
    end
    if config.wrapper_output then
        table.insert(config.outputs, config.wrapper_output)
    end
    if config.header_output then
        table.insert(config.outputs, config.header_output)
    end
    if config.public_header then
        table.insert(config.outputs, config.public_header)
    end
    if config.public_module then
        table.insert(config.outputs, config.public_module)
    end
    for _, output in ipairs(config.module_wrapper_outputs or {}) do
        table.insert(config.outputs, output)
    end
    for _, output in ipairs(config.module_header_outputs or {}) do
        table.insert(config.outputs, output)
    end
    config.depcache_anchor = config.header_output or config.module_header_outputs[1]
    config.lookup_guard_output = config.depcache_anchor .. ".lookup_guards.json"
    table.insert(config.outputs, config.lookup_guard_output)
    config.prepare_inputs = prepare_mock_codegen_inputs
    local dep_targets = collect_dep_targets(opts.deps)

    set_policy("build.fence", true)
    set_configdir(project_root())
    local fmt_link = gentest_fmt_link_name()
    if not fmt_link then
        add_packages("fmt")
    end
    add_includedirs(incdirs())
    add_includedirs(gentest_public_include_dir(), {public = true})
    local public_linkdirs = gentest_public_linkdirs()
    if #public_linkdirs > 0 then
        add_linkdirs(table.unpack(public_linkdirs), {public = true})
    end
    local runtime_link = gentest_runtime_link_name()
    if runtime_link then
        add_links(runtime_link)
    end
    if fmt_link then
        add_links(fmt_link)
    end
    if kind == "modules" then
        local module_link = gentest_module_link_name()
        if module_link then
            add_links(module_link)
        end
    end
    add_includedirs(out_dir_abs, {public = true})
    add_defines(gentest_common_defines())
    if opts.defines and #opts.defines > 0 then
        add_defines(table.unpack(opts.defines))
    end
    add_cxxflags(table.unpack(gentest_common_cxxflags()), {force = true})
    if opts.clang_args and #opts.clang_args > 0 then
        add_cxxflags(table.unpack(opts.clang_args), {force = true})
    end
    if kind == "modules" then
        for _, entry in ipairs(config.public_module_entries or {}) do
            add_files(entry.output_rel, {public = true, always_added = true})
        end
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
        add_headerfiles(public_header, {public = true, always_added = true})
    end
    local generated_support_headers = {config.mock_registry, config.mock_impl}
    for _, generated_header in ipairs(config.mock_domain_registry_outputs) do
        table.insert(generated_support_headers, generated_header)
    end
    for _, generated_header in ipairs(config.mock_domain_impl_outputs) do
        table.insert(generated_support_headers, generated_header)
    end
    if config.header_output then
        table.insert(generated_support_headers, config.header_output)
    end
    for _, generated_header in ipairs(config.module_header_outputs or {}) do
        table.insert(generated_support_headers, generated_header)
    end
    add_headerfiles(table.unpack(generated_support_headers), {always_added = true})
    if kind ~= "modules" then
        -- Textual definitions are existing non-compilable inputs, so they are
        -- safe clean-tree preparation triggers.
        add_files(config.defs[1], {rules = "gentest.codegen.prepare", override = true, always_added = true})
    end
    if config.depfile then
        add_extrafiles(config.depfile, {always_added = true})
    end
    if config.aggregate_source then
        add_extrafiles(config.aggregate_source, {always_added = true})
    end
    if opts.headerfiles and #opts.headerfiles > 0 then
        add_headerfiles(table.unpack(opts.headerfiles))
    else
        add_headerfiles(table.unpack(defs))
    end
    if #dep_targets > 0 then
        add_deps(table.unpack(dep_targets))
    end
    on_config(function ()
        ensure_fallback_compdb(config.fallback_compdb_file, os, io)
        ensure_materialized_public_modules(config.public_module_entries, os)
        if config.aggregate_source then
            ensure_textual_mock_aggregate(config.aggregate_source, defs, os, io)
        end
    end)
    on_load(function (target)
        local dep_include_dirs = resolve_dep_inputs(config.deps)
        for _, include_dir in ipairs(dep_include_dirs) do
            target:add("includedirs", include_dir)
            append_unique(include_dirs, seen_registered_includes, include_dir)
        end
    end)
    after_load(function (target)
        if kind == "modules" then
            -- Xmake 3.0.6 can retain a c++.build source-batch shape across
            -- separate dependency/consumer builds even after generated module
            -- sources change. Invalidate this target and its actual downstream
            -- dependency graph, using canonical Xmake full target names, so
            -- their scans are recomputed before graph construction and
            -- independently of generated-file timestamp resolution.
            local project = import("core.project.project")
            local localcache = import("core.cache.localcache")
            invalidate_xmake_module_scanner_cache(target, project, localcache)
        end
    end)
    register_generation_config(target_name, config)
    if kind == "modules" then
        before_preparecmd(function(target, batchcmds)
            run_cached_codegen(target, batchcmds, config)
        end)
    end

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

    gentest_apply_windows_llvm_toolchain()
    gentest_apply_compiler_cache_policy(kind)

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
        wrapper_cpp = module_registration_output_rel(output_dir, source, 0)
        wrapper_h = module_header_output_rel(output_dir, source, 0)
    end
    local wrapper_d = path.join(output_dir, basename_stem(wrapper_h) .. ".d")
    local fallback_compdb_dir, fallback_compdb_file = fallback_compdb_paths(output_dir)
    local extra_includes = {}
    local seen_extra_includes = {}
    for _, include_dir in ipairs(opts.includes or {}) do
        append_unique(extra_includes, seen_extra_includes, include_dir)
    end
    local dep_targets = collect_dep_targets(opts.deps)
    local config = {
        kind = kind,
        operation = "gentest_attach_codegen",
        codegen_kind = "suite",
        out_dir_abs = out_dir_abs,
        fallback_compdb_dir = fallback_compdb_dir,
        fallback_compdb_file = fallback_compdb_file,
        wrapper_output = project_path(wrapper_cpp),
        registration_output = project_path(wrapper_cpp),
        header_output = project_path(wrapper_h),
        artifact_manifest = project_path(path.join(output_dir, sanitize_target_id(target_name) .. ".artifact_manifest.json")),
        compile_context_id = sanitize_target_id(target_name) .. ":" .. project_path(source),
        depfile = project_path(wrapper_d),
        source_file = project_path(source),
        extra_includes = extra_includes,
        dep_module_sources = {},
        deps = opts.deps or {},
        defines = opts.defines or {},
        clang_args = opts.clang_args or {},
        public_modules_via_deps = opts.public_modules_via_deps == true,
        public_module_entries = {},
    }
    if kind == "modules" then
        config.module_name = require_opt(opts, "module_name", "gentest_attach_codegen(kind='modules')")
    end
    if kind == "modules" and not config.public_modules_via_deps then
        config.public_module_entries = materialized_public_module_entries(output_dir)
    end
    config.inputs = {config.source_file}
    config.outputs = {
        config.depfile,
        config.header_output,
        config.artifact_manifest,
    }
    if kind == "modules" then
        table.insert(config.outputs, config.registration_output)
    else
        table.insert(config.outputs, config.wrapper_output)
    end
    config.prepare_inputs = prepare_suite_codegen_inputs
    config.depcache_anchor = config.header_output
    config.lookup_guard_output = config.depcache_anchor .. ".lookup_guards.json"
    table.insert(config.outputs, config.lookup_guard_output)
    set_configdir(project_root())
    local fmt_link = gentest_fmt_link_name()
    if not fmt_link then
        add_packages("fmt")
    end
    add_includedirs(incdirs())
    add_includedirs(gentest_public_include_dir(), {public = true})
    local public_linkdirs = gentest_public_linkdirs()
    if #public_linkdirs > 0 then
        add_linkdirs(table.unpack(public_linkdirs), {public = true})
    end
    local runtime_link = gentest_runtime_link_name()
    if runtime_link then
        add_links(runtime_link)
    end
    if fmt_link then
        add_links(fmt_link)
    end
    if kind == "modules" then
        local module_link = gentest_module_link_name()
        if module_link then
            add_links(module_link)
        end
    end
    add_defines(gentest_common_defines())
    if opts.defines and #opts.defines > 0 then
        add_defines(table.unpack(opts.defines))
    end
    add_cxxflags(table.unpack(gentest_common_cxxflags()), {force = true})
    if opts.clang_args and #opts.clang_args > 0 then
        add_cxxflags(table.unpack(opts.clang_args), {force = true})
    end
    if kind == "modules" then
        for _, entry in ipairs(config.public_module_entries or {}) do
            add_files(entry.output_rel, {public = true, always_added = true})
        end
    end
    if kind == "modules" then
        add_files(source, {public = true, always_added = true})
        add_files(wrapper_cpp, {always_added = true})
    else
        add_files(wrapper_cpp, {always_added = true})
        -- The owner source is deliberately not added: the generated textual
        -- wrapper includes it and is the sole compiled TU. Use an existing
        -- non-compilable Gentest header to drive preparation on clean trees.
        add_files(path.join(gentest_public_include_dir(), "gentest", "attributes.h"), {
            rules = "gentest.codegen.prepare",
            override = true,
            always_added = true,
        })
    end
    if main_source then
        add_files(main_source, {always_added = true})
    end
    add_headerfiles(config.header_output, {always_added = true})
    add_extrafiles(config.artifact_manifest, config.depfile, {always_added = true})
    if #dep_targets > 0 then
        add_deps(table.unpack(dep_targets))
    end
    on_config(function ()
        ensure_fallback_compdb(config.fallback_compdb_file, os, io)
        ensure_materialized_public_modules(config.public_module_entries, os)
    end)
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
    register_generation_config(target_name, config)
    if kind == "modules" then
        before_preparecmd(function(target, batchcmds)
            run_cached_codegen(target, batchcmds, config)
        end)
    end

end

function gentest_add_public_modules(opts)
    require_clang_module_toolchain(nil, "gentest_add_public_modules")
    gentest_apply_windows_llvm_toolchain()
    gentest_apply_compiler_cache_policy("modules")

    local output_dir = require_opt(opts, "output_dir", "gentest_add_public_modules")
    local out_dir_abs = project_path(output_dir)
    local public_module_entries = materialized_public_module_entries(output_dir)
    if #public_module_entries == 0 then
        fail("gentest_add_public_modules requires gentest_configure({ gentest_module_files = {...} })")
    end

    set_policy("build.fence", true)
    set_configdir(project_root())
    if not gentest_fmt_link_name() then
        add_packages("fmt")
    end
    add_includedirs(incdirs())
    add_includedirs(gentest_public_include_dir(), {public = true})
    add_includedirs(out_dir_abs, {public = true})
    add_defines(gentest_common_defines())
    add_cxxflags(table.unpack(gentest_common_cxxflags()), {force = true})
    if opts.defines and #opts.defines > 0 then
        add_defines(table.unpack(opts.defines))
    end
    if opts.clang_args and #opts.clang_args > 0 then
        add_cxxflags(table.unpack(opts.clang_args), {force = true})
    end
    for _, entry in ipairs(public_module_entries) do
        add_files(entry.output_rel, {public = true, always_added = true})
    end
    on_config(function ()
        ensure_materialized_public_modules(public_module_entries, os)
    end)
end
