-- Serialize a target's cache recheck, codegen process, and snapshot publish.
-- This prevents concurrent builds with different effective identities from
-- attributing another process's generated outputs to the wrong identity.

import("codegen_dep_cache_common", {rootdir = os.scriptdir()})
import("update_codegen_dep_cache", {rootdir = os.scriptdir()})
import("core.base.json")

local function append_unique(result, seen, filepath, project_root)
    if not filepath or filepath == "" then
        return
    end
    local normalized = tostring(filepath)
    if not path.is_absolute(normalized) then
        normalized = path.join(project_root, normalized)
    end
    normalized = path.absolute(normalized)
    if not seen[normalized] then
        seen[normalized] = true
        table.insert(result, normalized)
    end
end

local function load_lookup_guards(sidecar)
    if not sidecar or sidecar == "" then
        return {guards = {}, configured_roots = {}}
    end
    local loaded, report = utils.trycall(function()
        return json.loadfile(sidecar)
    end)
    if not loaded or type(report) ~= "table" or report.schema ~= "gentest.lookup_guards.v2" or report.complete ~= true or
        type(report.guards) ~= "table" or type(report.configured_roots) ~= "table" then
        cprint("${yellow}warning: Gentest lookup guards are incomplete; codegen will run again on the next build")
        return nil
    end
    local guards = {}
    for _, guard in ipairs(report.guards) do
        if type(guard) ~= "string" or guard == "" then
            cprint("${yellow}warning: Gentest lookup guard sidecar is invalid; codegen will run again on the next build")
            return nil
        end
        table.insert(guards, guard)
    end
    local configured_roots = {}
    for _, root in ipairs(report.configured_roots) do
        if type(root) ~= "table" or type(root.path) ~= "string" or root.path == "" or
            (root.state ~= "missing" and root.state ~= "directory") then
            cprint("${yellow}warning: Gentest configured include-root metadata is invalid; codegen will run again on the next build")
            return nil
        end
        table.insert(configured_roots, {path = root.path, state = root.state})
    end
    return {guards = guards, configured_roots = configured_roots}
end

function main(cache_path, depfile, lookup_guard_output, identity, project_root, static_count_text, program, ...)
    local static_count = tonumber(static_count_text)
    if not static_count or static_count < 0 then
        raise("invalid Gentest codegen dependency count")
    end
    local values = {...}
    if #values < static_count then
        raise("incomplete Gentest codegen cache invocation")
    end

    local static_dependencies = {}
    for index = 1, static_count do
        table.insert(static_dependencies, values[index])
    end
    local codegen_args = {}
    for index = static_count + 1, #values do
        table.insert(codegen_args, values[index])
    end

    update_codegen_dep_cache.with_cache_lock(cache_path, true, function()
        if update_codegen_dep_cache.snapshot_current(cache_path, identity) then
            return true
        end

        cprint("${dim}gentest-codegen-cache-miss${clear}")
        os.vrunv(program, codegen_args)

        local lookup_metadata = load_lookup_guards(lookup_guard_output)
        if lookup_metadata == nil then
            return false
        end

        local files = {}
        local seen = {}
        for _, filepath in ipairs(codegen_dep_cache_common.dependencies(depfile, project_root)) do
            append_unique(files, seen, filepath, project_root)
        end
        for _, filepath in ipairs(static_dependencies) do
            append_unique(files, seen, filepath, project_root)
        end
        return update_codegen_dep_cache.save_snapshot_locked(cache_path, identity, files, lookup_metadata)
    end)
end
