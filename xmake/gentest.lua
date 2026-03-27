local gentest_state = {}

local function fail(message)
    print("error: " .. message)
    if is_host("windows") then
        os.vrunv("cmd", {"/c", "exit", "1"})
    else
        os.vrunv("false", {})
    end
end

-- Configure the shared Xmake helper context. External consumers can override
-- codegen_project_root to point at a gentest checkout, or set GENTEST_CODEGEN
-- to use a prebuilt generator directly.
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

local function ensure_parent_dir(mkdir_fn, filepath)
    local dirpath = path.directory(filepath)
    if dirpath and dirpath ~= "" then
        mkdir_fn(dirpath)
    end
end

local function write_generated_file(mkdir_fn, writefile_fn, relpath, contents)
    local filepath = project_path(relpath)
    ensure_parent_dir(mkdir_fn, filepath)
    writefile_fn(filepath, contents)
    return filepath
end

local function copy_generated_file(mkdir_fn, copy_fn, relpath, source_relpath)
    local filepath = project_path(relpath)
    ensure_parent_dir(mkdir_fn, filepath)
    copy_fn(project_path(source_relpath), filepath)
    return filepath
end

local function read_project_file(readfile_fn, relpath)
    return readfile_fn(project_path(relpath))
end

local function relative_include(from_relpath, to_relpath)
    return path.translate(path.relative(project_path(to_relpath), path.directory(project_path(from_relpath))))
end

local function suite_wrapper_placeholder(wrapper_rel, source_rel, header_rel)
    return table.concat({
        "// generated placeholder",
        "",
        "#include \"" .. relative_include(wrapper_rel, source_rel) .. "\"",
        "",
        "#if !defined(GENTEST_CODEGEN) && __has_include(\"" .. path.filename(header_rel) .. "\")",
        "#include \"" .. path.filename(header_rel) .. "\"",
        "#endif",
        "",
    }, "\n")
end

local function textual_mock_source_placeholder(wrapper_rel, defs, header_rel)
    local lines = {
        "// generated placeholder",
        "",
        "#include \"gentest/mock.h\"",
    }
    for _, defs_file in ipairs(defs) do
        table.insert(lines, "#include \"" .. relative_include(wrapper_rel, defs_file) .. "\"")
    end
    table.insert(lines, "")
    table.insert(lines, "#if !defined(GENTEST_CODEGEN) && __has_include(\"" .. path.filename(header_rel) .. "\")")
    table.insert(lines, "#include \"" .. path.filename(header_rel) .. "\"")
    table.insert(lines, "#endif")
    table.insert(lines, "")
    return table.concat(lines, "\n")
end

local function textual_public_header_placeholder(public_header_rel, defs, mock_registry_rel, mock_impl_rel)
    local lines = {
        "// generated placeholder",
        "#pragma once",
        "",
        "#define GENTEST_NO_AUTO_MOCK_INCLUDE 1",
        "#include \"gentest/mock.h\"",
    }
    for _, defs_file in ipairs(defs) do
        table.insert(lines, "#include \"" .. relative_include(public_header_rel, defs_file) .. "\"")
    end
    table.insert(lines, "#undef GENTEST_NO_AUTO_MOCK_INCLUDE")
    table.insert(lines, "")
    table.insert(lines, "#include \"" .. path.filename(mock_registry_rel) .. "\"")
    table.insert(lines, "#include \"" .. path.filename(mock_impl_rel) .. "\"")
    table.insert(lines, "")
    return table.concat(lines, "\n")
end

local function header_placeholder()
    return table.concat({
        "// generated placeholder",
        "#pragma once",
        "",
    }, "\n")
end

local function parse_module_name(module_source_text)
    local module_name = module_source_text:match("[\r\n]%s*export%s+module%s+([^;]+)%s*;")
    if not module_name then
        module_name = module_source_text:match("^%s*export%s+module%s+([^;]+)%s*;")
    end
    if not module_name then
        module_name = module_source_text:match("[\r\n]%s*module%s+([^;]+)%s*;")
    end
    if not module_name then
        module_name = module_source_text:match("^%s*module%s+([^;]+)%s*;")
    end
    if not module_name then
        fail("unable to determine module name from module source")
    end
    return module_name:gsub("^%s+", ""):gsub("%s+$", "")
end

local function module_aggregate_placeholder(readfile_fn, module_name, defs)
    local lines = {
        "// generated placeholder",
        "module;",
        "",
        "export module " .. module_name .. ";",
        "",
        "export import gentest;",
        "export import gentest.mock;",
    }
    for _, defs_file in ipairs(defs) do
        table.insert(lines, "export import " .. parse_module_name(read_project_file(readfile_fn, defs_file)) .. ";")
    end
    table.insert(lines, "")
    return table.concat(lines, "\n")
end

local function anchor_placeholder(target_id)
    return table.concat({
        "// generated placeholder",
        "namespace gentest::detail {",
        "int " .. anchor_symbol_name(target_id) .. " = 0;",
        "} // namespace gentest::detail",
        "",
    }, "\n")
end

local function json_escape(text)
    return (text or ""):gsub("\\", "\\\\"):gsub('"', '\\"'):gsub("\n", "\\n")
end

local function json_unescape(text)
    return (text or ""):gsub("\\n", "\n"):gsub('\\"', '"'):gsub("\\\\", "\\")
end

local function decode_string_array(metadata_text, key)
    local values = {}
    local body = metadata_text:match('"' .. key .. '"%s*:%s*%[(.-)%]')
    if not body then
        return values
    end
    for value in body:gmatch('"(.-)"') do
        table.insert(values, json_unescape(value))
    end
    return values
end

local function decode_string_value(metadata_text, key)
    local value = metadata_text:match('"' .. key .. '"%s*:%s*"(.-)"')
    if not value then
        return nil
    end
    return json_unescape(value)
end

local function decode_module_sources(metadata_text)
    local values = {}
    local body = metadata_text:match('"module_sources"%s*:%s*%[(.-)%]')
    if not body then
        return values
    end
    for object_body in body:gmatch("%b{}") do
        local module_name = object_body:match('"module_name"%s*:%s*"(.-)"')
        local module_path = object_body:match('"path"%s*:%s*"(.-)"')
        if module_name and module_path then
            table.insert(values, {
                module_name = json_unescape(module_name),
                path = json_unescape(module_path),
            })
        end
    end
    return values
end

local function decode_public_surface(metadata_text)
    local body = metadata_text:match('"public_surface"%s*:%s*(%b{})')
    if not body then
        return {}
    end
    local payload = {}
    for _, key in ipairs({"type", "path", "module_name"}) do
        local value = decode_string_value(body, key)
        if value then
            payload[key] = value
        end
    end
    return payload
end

local function encode_string_array(values)
    local encoded = {}
    for _, value in ipairs(values or {}) do
        table.insert(encoded, '"' .. json_escape(value) .. '"')
    end
    return "[" .. table.concat(encoded, ", ") .. "]"
end

local function encode_module_sources(values)
    local encoded = {}
    for _, value in ipairs(values or {}) do
        table.insert(
            encoded,
            '{"module_name":"' .. json_escape(value.module_name or "") .. '","path":"' .. json_escape(value.path or "") .. '"}'
        )
    end
    return "[" .. table.concat(encoded, ", ") .. "]"
end

local function encode_mock_metadata(payload)
    local fragments = {
        '{',
        '"schema_version": ' .. tostring(payload.schema_version or 1),
        ', "mode": "' .. json_escape(payload.mode or "mocks") .. '"',
        ', "backend": "' .. json_escape(payload.backend or "xmake") .. '"',
        ', "kind": "' .. json_escape(payload.kind or "textual") .. '"',
        ', "target_id": "' .. json_escape(payload.target_id or "") .. '"',
        ', "out_dir": "' .. json_escape(payload.out_dir or "") .. '"',
        ', "include_dirs": ' .. encode_string_array(payload.include_dirs or {}),
        ', "public_surface": {"type":"' .. json_escape((payload.public_surface or {}).type or "") .. '"',
    }
    if payload.public_surface and payload.public_surface.path then
        table.insert(fragments, ',"path":"' .. json_escape(payload.public_surface.path) .. '"')
    end
    if payload.public_surface and payload.public_surface.module_name then
        table.insert(fragments, ',"module_name":"' .. json_escape(payload.public_surface.module_name) .. '"')
    end
    table.insert(fragments, "}")
    table.insert(fragments, ', "module_sources": ' .. encode_module_sources(payload.module_sources or {}))
    table.insert(fragments, ', "support_headers": ' .. encode_string_array(payload.support_headers or {}))
    table.insert(fragments, "}")
    return table.concat(fragments)
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
    local metadata_paths = {}
    local seen_includes = {}
    local seen_metadata = {}
    local seen_targets = {}
    local metadata_by_target = registered_target_metadata()

    local function visit_dep(dep)
        if type(dep) == "table" then
            if dep.metadata_path then
                append_unique(metadata_paths, seen_metadata, dep.metadata_path)
            end
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
                append_unique(metadata_paths, seen_metadata, registered.metadata_path)
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
    return include_dirs, metadata_paths
end

local function load_mock_metadata(metadata_path)
    local metadata_text = io.readfile(metadata_path)
    if not metadata_text then
        fail("failed to read mock metadata `" .. metadata_path .. "`")
    end
    return {
        schema_version = tonumber(metadata_text:match('"schema_version"%s*:%s*(%d+)') or "1"),
        mode = decode_string_value(metadata_text, "mode"),
        backend = decode_string_value(metadata_text, "backend"),
        kind = decode_string_value(metadata_text, "kind"),
        target_id = decode_string_value(metadata_text, "target_id"),
        out_dir = decode_string_value(metadata_text, "out_dir"),
        include_dirs = decode_string_array(metadata_text, "include_dirs"),
        public_surface = decode_public_surface(metadata_text),
        module_sources = decode_module_sources(metadata_text),
        support_headers = decode_string_array(metadata_text, "support_headers"),
    }
end

local function collect_mock_metadata_inputs(metadata_paths)
    local include_dirs = {}
    local module_sources = {}
    local support_headers = {}
    local seen_includes = {}
    local seen_modules = {}
    local seen_headers = {}
    for _, metadata_path in ipairs(metadata_paths or {}) do
        local payload = load_mock_metadata(metadata_path)
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
    return include_dirs, module_sources, support_headers
end

local function merge_generated_mock_metadata(metadata_path, dep_metadata_paths)
    if not dep_metadata_paths or #dep_metadata_paths == 0 then
        return
    end
    local payload = load_mock_metadata(metadata_path)
    local dep_include_dirs, dep_module_sources, dep_support_headers = collect_mock_metadata_inputs(dep_metadata_paths)

    local include_dirs = {}
    local seen_includes = {}
    for _, include_dir in ipairs(payload.include_dirs or {}) do
        append_unique(include_dirs, seen_includes, include_dir)
    end
    for _, include_dir in ipairs(dep_include_dirs) do
        append_unique(include_dirs, seen_includes, include_dir)
    end

    local support_headers = {}
    local seen_support_headers = {}
    for _, support_header in ipairs(payload.support_headers or {}) do
        append_unique(support_headers, seen_support_headers, support_header)
    end
    for _, support_header in ipairs(dep_support_headers) do
        append_unique(support_headers, seen_support_headers, support_header)
    end

    local module_sources = {}
    local seen_module_sources = {}
    for _, module_source in ipairs(payload.module_sources or {}) do
        if type(module_source) == "table" then
            local module_name = module_source.module_name or ""
            local module_path = module_source.path or ""
            if module_name ~= "" and module_path ~= "" then
                local key = module_name .. "=" .. module_path
                if not seen_module_sources[key] then
                    seen_module_sources[key] = true
                    table.insert(module_sources, {module_name = module_name, path = module_path})
                end
            end
        end
    end
    for _, mapping in ipairs(dep_module_sources) do
        local module_name, module_path = mapping:match("^([^=]+)=(.+)$")
        if module_name and module_path then
            local key = module_name .. "=" .. module_path
            if not seen_module_sources[key] then
                seen_module_sources[key] = true
                table.insert(module_sources, {module_name = module_name, path = module_path})
            end
        end
    end

    payload.include_dirs = include_dirs
    payload.module_sources = module_sources
    payload.support_headers = support_headers
    io.writefile(metadata_path, encode_mock_metadata(payload) .. "\n")
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

local function is_clang_tool(toolpath)
    local toolname = path.filename(toolpath or ""):lower()
    toolname = toolname:gsub("%.exe$", "")
    return toolname == "clang++" or toolname == "clang" or toolname == "clang-cl" or toolname:match("^clang%+%+%-%d+$") or
               toolname:match("^clang%-%d+$")
end

local function configured_cxx_tool_hint()
    return get_config("cxx") or os.getenv("CXX") or ""
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
        operation .. "(kind='modules') requires a Clang C++ toolchain in Xmake. "
            .. "Configure Xmake with CC=clang and CXX=clang++ (or clang-cl on Windows). "
            .. "Resolved cxx tool: `" .. tostring(cxx_tool) .. "`"
    )
end

local function run_command(batchcmds, program, args)
    if batchcmds then
        batchcmds:vrunv(program, args)
    else
        os.vrunv(program, args)
    end
end

local function resolve_codegen()
    local env_path = os.getenv("GENTEST_CODEGEN")
    if env_path then
        local resolved_env_path = env_path
        if not os.isfile(resolved_env_path) then
            local rel_to_project = path.relative(resolved_env_path, project_root())
            if rel_to_project and rel_to_project ~= resolved_env_path and not rel_to_project:find("^%.%.") then
                local remapped = path.join(codegen_project_root(), rel_to_project)
                if os.isfile(remapped) then
                    resolved_env_path = remapped
                end
            end
        end
        if os.isfile(resolved_env_path) then
            local env_compdb_dir = nil
            local candidate = path.directory(resolved_env_path)
            local remaining = 8
            while candidate and remaining > 0 do
                if os.isfile(path.join(candidate, "compile_commands.json")) then
                    env_compdb_dir = candidate
                    break
                end
                local parent = path.directory(candidate)
                if not parent or parent == candidate then
                    break
                end
                candidate = parent
                remaining = remaining - 1
            end
            return resolved_env_path, env_compdb_dir, nil
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

local function ensure_codegen(batchcmds)
    local cached = gentest_state["_resolved_codegen"]
    if cached and cached.path and os.isfile(cached.path) then
        return cached.path, cached.compdb_dir
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
        for _, module_source in ipairs(default_external_module_sources()) do
            table.insert(args, "--external-module-source")
            table.insert(args, module_source)
        end
        for _, module_source in ipairs(config.dep_module_sources or {}) do
            table.insert(args, "--external-module-source")
            table.insert(args, module_source)
        end
    end
    for _, include_dir in ipairs(resolved_incdirs()) do
        table.insert(args, "--include-root")
        table.insert(args, include_dir)
    end
    for _, defs_file in ipairs(config.defs) do
        table.insert(args, "--defs-file")
        table.insert(args, defs_file)
    end
    for _, metadata_path in ipairs(config.metadata_paths or {}) do
        table.insert(args, "--mock-metadata")
        table.insert(args, metadata_path)
    end
    if compdb_dir then
        table.insert(args, "--compdb")
        table.insert(args, compdb_dir)
    end
    append_common_codegen_clang_args(args, config.extra_includes, config.defines, config.clang_args)
    run_command(batchcmds, python_program(), args)
end

local function run_suite_codegen(batchcmds, codegen, compdb_dir, config)
    local args = {
        buildsystem_codegen(),
        "--backend", "xmake",
        "--mode", "suite",
        "--kind", config.kind,
        "--codegen", codegen,
        "--source-root", project_root(),
        "--out-dir", config.out_dir_abs,
        "--wrapper-output", config.wrapper_output,
        "--header-output", config.header_output,
        "--depfile", config.depfile,
        "--source-file", config.source_file,
    }
    if config.kind == "modules" then
        for _, include_dir in ipairs(resolved_incdirs()) do
            table.insert(args, "--include-root")
            table.insert(args, include_dir)
        end
        for _, module_source in ipairs(default_external_module_sources()) do
            table.insert(args, "--external-module-source")
            table.insert(args, module_source)
        end
    end
    for _, metadata_path in ipairs(config.metadata_paths) do
        table.insert(args, "--mock-metadata")
        table.insert(args, metadata_path)
    end
    if compdb_dir then
        table.insert(args, "--compdb")
        table.insert(args, compdb_dir)
    end
    append_common_codegen_clang_args(args, config.extra_includes, config.defines, config.clang_args)
    run_command(batchcmds, python_program(), args)
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
        extra_includes = {},
        metadata_paths = {},
        dep_module_sources = {},
        deps = opts.deps or {},
        defines = opts.defines or {},
        clang_args = opts.clang_args or {},
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
        for index, defs_file in ipairs(defs) do
            local zero_index = index - 1
            local staged_rel = module_defs_stage_rel(output_dir, defs_file, zero_index)
            local wrapper_rel = module_wrapper_output_rel(output_dir, staged_rel, zero_index)
            table.insert(add_public_files, wrapper_rel)
        end
        public_module = module_public_output_rel(output_dir, module_name)
        table.insert(add_private_files, anchor_cpp)
    end
    for _, defs_file in ipairs(defs) do
        table.insert(config.defs, project_path(defs_file))
    end
    local dep_targets = collect_dep_targets(opts.deps)

    set_policy("build.fence", true)
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
    on_load(function (target)
        if kind == "textual" then
            write_generated_file(os.mkdir, io.writefile, defs_cpp, textual_mock_source_placeholder(defs_cpp, defs, codegen_h))
            write_generated_file(os.mkdir, io.writefile, anchor_cpp, anchor_placeholder(target_id))
            write_generated_file(
                os.mkdir,
                io.writefile,
                public_header,
                textual_public_header_placeholder(public_header, defs, mock_registry_h, mock_impl_h)
            )
            write_generated_file(os.mkdir, io.writefile, mock_registry_h, header_placeholder())
            write_generated_file(os.mkdir, io.writefile, mock_impl_h, header_placeholder())
        else
            for index, defs_file in ipairs(defs) do
                local zero_index = index - 1
                local staged_rel = module_defs_stage_rel(output_dir, defs_file, zero_index)
                local wrapper_rel = module_wrapper_output_rel(output_dir, staged_rel, zero_index)
                write_generated_file(os.mkdir, io.writefile, module_header_output_rel(output_dir, staged_rel, zero_index), header_placeholder())
                copy_generated_file(os.mkdir, os.cp, wrapper_rel, defs_file)
            end
            write_generated_file(
                os.mkdir,
                io.writefile,
                module_public_output_rel(output_dir, config.module_name),
                module_aggregate_placeholder(io.readfile, config.module_name, defs)
            )
            write_generated_file(os.mkdir, io.writefile, anchor_cpp, anchor_placeholder(target_id))
            write_generated_file(os.mkdir, io.writefile, mock_registry_h, header_placeholder())
            write_generated_file(os.mkdir, io.writefile, mock_impl_h, header_placeholder())
        end

        local dep_include_dirs, dep_metadata_paths = resolve_dep_inputs(config.deps)
        config.metadata_paths = dep_metadata_paths
        for _, include_dir in ipairs(dep_include_dirs) do
            target:add("includedirs", include_dir)
            append_unique(include_dirs, seen_registered_includes, include_dir)
        end
    end)
    before_buildcmd(function (target, batchcmds)
        if kind == "modules" then
            require_clang_module_toolchain(target, "gentest_add_mocks")
        end
        local codegen, compdb_dir = ensure_codegen(batchcmds)
        config.extra_includes = collect_target_package_include_dirs(target)
        local dep_include_dirs, dep_metadata_paths = resolve_dep_inputs(config.deps)
        config.metadata_paths = dep_metadata_paths
        local dep_metadata_include_dirs, dep_module_sources = collect_mock_metadata_inputs(config.metadata_paths)
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
        run_mock_codegen(batchcmds, codegen, compdb_dir, config)
        merge_generated_mock_metadata(config.metadata_output, config.metadata_paths)
    end)

    registered_target_metadata()[target_name] = {
        target = target_name,
        include_dir = out_dir_abs,
        include_dirs = include_dirs,
        metadata_path = config.metadata_output,
        deps = opts.deps or {},
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
    local dep_targets = collect_dep_targets(opts.deps)
    local config = {
        kind = kind,
        out_dir_abs = out_dir_abs,
        wrapper_output = project_path(wrapper_cpp),
        header_output = project_path(wrapper_h),
        depfile = project_path(wrapper_d),
        source_file = project_path(source),
        extra_includes = extra_includes,
        metadata_paths = {},
        deps = opts.deps or {},
        defines = opts.defines or {},
        clang_args = opts.clang_args or {},
    }

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
    on_load(function (target)
        if kind == "textual" then
            write_generated_file(os.mkdir, io.writefile, wrapper_cpp, suite_wrapper_placeholder(wrapper_cpp, source, wrapper_h))
        else
            write_generated_file(os.mkdir, io.writefile, wrapper_h, header_placeholder())
            copy_generated_file(os.mkdir, os.cp, wrapper_cpp, source)
        end

        local dep_include_dirs, dep_metadata_paths = resolve_dep_inputs(config.deps)
        config.metadata_paths = dep_metadata_paths
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
        local codegen, compdb_dir = ensure_codegen(batchcmds)
        local dep_include_dirs, dep_metadata_paths = resolve_dep_inputs(config.deps)
        config.metadata_paths = dep_metadata_paths
        for _, include_dir in ipairs(dep_include_dirs) do
            append_unique(config.extra_includes, seen_extra_includes, include_dir)
        end
        local package_include_dirs = collect_target_package_include_dirs(target)
        for _, include_dir in ipairs(package_include_dirs) do
            append_unique(config.extra_includes, seen_extra_includes, include_dir)
        end
        run_suite_codegen(batchcmds, codegen, compdb_dir, config)
    end)
end
