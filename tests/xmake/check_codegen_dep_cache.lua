local function fail(message)
    raise("Gentest Xmake dependency-cache test failed: " .. message)
end

local function require_equal(actual, expected, label)
    if actual ~= expected then
        fail(label .. "\nexpected: " .. tostring(expected) .. "\nactual:   " .. tostring(actual))
    end
end

local function make_escape(value)
    return value:gsub("([ \t#%$:\\])", "\\%1")
end

local function run_parser_check(helper, work_dir)
    import("codegen_dep_cache_common", {rootdir = path.directory(helper)})

    os.mkdir(work_dir)
    local depfile = path.join(work_dir, "synthetic.d")
    local escaped_path = path.join(work_dir, "escaped path.hpp")
    local literal_path = is_host("windows") and "Z:\\gentest\\literal\\private.hpp" or "literal\\private.hpp"
    io.writefile(depfile, "output: " .. make_escape(escaped_path) .. " " .. literal_path .. "\n")

    local dependencies = codegen_dep_cache_common.dependencies(depfile, work_dir)
    require_equal(#dependencies, 2, "dependency count")
    require_equal(dependencies[1], path.absolute(escaped_path), "recognized Make escapes")
    local expected_literal = literal_path
    if not path.is_absolute(expected_literal) then
        expected_literal = path.join(work_dir, expected_literal)
    end
    require_equal(dependencies[2], path.absolute(expected_literal), "literal backslash and drive spelling")
end

local function validate_snapshot(cache_path, identity)
    local candidates = os.files(cache_path .. ".v.*") or {}
    require_equal(#candidates, 1, "published snapshot count")
    local loaded = io.load(candidates[1])
    if type(loaded) ~= "table" or loaded.schema ~= 1 or loaded.identity ~= identity or type(loaded.files) ~= "table" then
        fail("published snapshot is invalid")
    end
    for _, entry in ipairs(loaded.files) do
        if type(entry) ~= "table" or type(entry.path) ~= "string" or entry.mtime ~= os.mtime(entry.path) then
            fail("published snapshot contains an invalid file fingerprint")
        end
    end
    for _, temporary in ipairs(os.files(cache_path .. ".next.*") or {}) do
        fail("temporary snapshot leaked after concurrent publication: " .. temporary)
    end
end

function main(mode, ...)
    if mode == "parser" then
        run_parser_check(...)
    elseif mode == "validate" then
        validate_snapshot(...)
    else
        fail("unknown mode " .. tostring(mode))
    end
end
