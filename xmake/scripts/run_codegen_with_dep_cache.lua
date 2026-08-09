-- Serialize a target's cache recheck, codegen process, and snapshot publish.
-- This prevents concurrent builds with different effective identities from
-- attributing another process's generated outputs to the wrong identity.

import("codegen_dep_cache_common", {rootdir = os.scriptdir()})
import("update_codegen_dep_cache", {rootdir = os.scriptdir()})

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

function main(cache_path, depfile, identity, project_root, static_count_text, program, ...)
    local static_count = tonumber(static_count_text)
    if not static_count or static_count < 0 then
        raise("invalid Gentest codegen static dependency count")
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

        os.vrunv(program, codegen_args)

        local files = {}
        local seen = {}
        for _, filepath in ipairs(codegen_dep_cache_common.dependencies(depfile, project_root)) do
            append_unique(files, seen, filepath, project_root)
        end
        for _, filepath in ipairs(static_dependencies) do
            append_unique(files, seen, filepath, project_root)
        end
        return update_codegen_dep_cache.save_snapshot_locked(cache_path, identity, files)
    end)
end
