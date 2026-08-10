-- Persist the dependency closure discovered by gentest_codegen. This runs
-- after codegen as a batch command, so a failed generator never replaces a
-- usable cache snapshot.

import("codegen_dep_cache_common", {rootdir = os.scriptdir()})

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

local function snapshot_name(cache_path, identity, entries)
    local hash = 2166136261
    local fingerprint = {}
    for _, entry in ipairs(entries) do
        table.insert(fingerprint, entry.kind .. ":" .. entry.path .. "=" .. tostring(entry.mtime or entry.state or "") ..
            ":" .. tostring(entry.target or ""))
    end
    local text = identity .. "\30" .. table.concat(fingerprint, "\31")
    for index = 1, #text do
        hash = ((hash ~ text:byte(index)) * 16777619) & 0xffffffff
    end
    return cache_path .. ".v." .. string.format("%08x", hash)
end

local function matching_snapshot(snapshot, identity, entries)
    if not os.isfile(snapshot) then
        return false
    end
    local loaded, state = utils.trycall(function()
        return io.load(snapshot)
    end)
    if not loaded or type(state) ~= "table" or state.schema ~= 3 or state.identity ~= identity or type(state.files) ~= "table" or
        #state.files ~= #entries then
        return false
    end
    for index, entry in ipairs(entries) do
        local cached = state.files[index]
        if type(cached) ~= "table" or cached.kind ~= entry.kind or cached.path ~= entry.path or cached.mtime ~= entry.mtime or
            cached.state ~= entry.state or cached.target ~= entry.target then
            return false
        end
    end
    return true
end

local function observe_root(raw_path)
    local current = path.absolute(raw_path)
    local seen = {}
    local depth = 0
    while os.islink(current) do
        if seen[current] or depth >= 64 then
            return nil
        end
        seen[current] = true
        local target = os.readlink(current)
        if not target or target == "" then
            return nil
        end
        current = path.is_absolute(target) and path.absolute(target) or path.absolute(path.join(path.directory(current), target))
        depth = depth + 1
    end
    if os.isdir(current) then
        return {state = "directory", target = current}
    end
    if os.exists(current) then
        return {state = "other", target = current}
    end
    return {state = "missing", target = current}
end

local function entry_current(entry)
    if type(entry) ~= "table" or type(entry.path) ~= "string" then
        return false
    end
    if entry.kind == "file" then
        return os.isfile(entry.path) and os.mtime(entry.path) == entry.mtime
    end
    if entry.kind == "absent" then
        return not os.exists(entry.path)
    end
    if entry.kind == "root" then
        local observed = observe_root(entry.path)
        return observed ~= nil and observed.state == entry.state and observed.target == entry.target
    end
    return false
end

function snapshot_current(cache_path, identity)
    for _, candidate in ipairs(os.files(cache_path .. ".v.*") or {}) do
        local loaded, state = utils.trycall(function()
            return io.load(candidate)
        end)
        if loaded and type(state) == "table" and state.schema == 3 and state.identity == identity and type(state.files) == "table" then
            local current = true
            for _, entry in ipairs(state.files) do
                if not entry_current(entry) then
                    current = false
                    break
                end
            end
            if current then
                return true
            end
        end
    end
    return false
end

local function append_lookup_metadata(entries, lookup_metadata)
    local seen = {}
    for _, entry in ipairs(entries) do
        seen[entry.path] = true
    end
    for _, guard in ipairs(lookup_metadata.guards or {}) do
        local normalized = path.absolute(guard)
        if not seen[normalized] then
            seen[normalized] = true
            if os.isfile(normalized) then
                table.insert(entries, {kind = "file", path = normalized, mtime = os.mtime(normalized)})
            elseif not os.exists(normalized) then
                table.insert(entries, {kind = "absent", path = normalized})
            else
                cprint("${yellow}warning: Gentest lookup guard is not a regular file or missing path: %s", normalized)
                return false
            end
        end
    end
    for _, root in ipairs(lookup_metadata.configured_roots or {}) do
        local normalized = path.absolute(root.path)
        local observed = observe_root(normalized)
        if not observed or observed.state == "other" or observed.state ~= root.state then
            cprint("${yellow}warning: Gentest configured include root changed or is not a directory/missing path: %s", normalized)
            return false
        end
        local seen_key = "root\31" .. normalized
        if not seen[seen_key] then
            seen[seen_key] = true
            table.insert(entries, {kind = "root", path = normalized, state = observed.state, target = observed.target})
        end
    end
    table.sort(entries, function(left, right)
        if left.kind ~= right.kind then
            return left.kind < right.kind
        end
        return left.path < right.path
    end)
    return true
end

function save_snapshot_locked(cache_path, identity, files, lookup_metadata)
    local entries = {}
    for _, filepath in ipairs(files) do
        if not os.isfile(filepath) then
            cprint("${yellow}warning: gentest codegen dependency cache omitted because a declared input/output is missing: %s", filepath)
            return false
        end
        table.insert(entries, {kind = "file", path = filepath, mtime = os.mtime(filepath)})
    end
    if not append_lookup_metadata(entries, lookup_metadata or {guards = {}, configured_roots = {}}) then
        return false
    end
    local snapshot = snapshot_name(cache_path, identity, entries)
    if matching_snapshot(snapshot, identity, entries) then
        return
    end

    -- Keep the versioned leaf immutable. A corrupt same-name leaf must not
    -- block recovery when unchanged generated outputs yield the same content
    -- fingerprint, so publish a fresh leaf and clean the invalid one later.
    if os.isfile(snapshot) then
        local repair_index = 1
        local repair_snapshot = snapshot .. ".repair." .. repair_index
        while os.isfile(repair_snapshot) do
            repair_index = repair_index + 1
            repair_snapshot = snapshot .. ".repair." .. repair_index
        end
        snapshot = repair_snapshot
    end

    local temporary_base = cache_path .. ".next." .. tostring(os.getpid()) .. "." .. tostring(os.mclock())
    local temporary = temporary_base
    local temporary_index = 0
    while os.exists(temporary) do
        temporary_index = temporary_index + 1
        temporary = temporary_base .. "." .. temporary_index
    end
    local invoked, ok_or_errors = utils.trycall(function()
        os.mkdir(path.directory(cache_path))
        io.save(temporary, {schema = 3, identity = identity, files = entries}, {orderkeys = true})
        if not os.isfile(temporary) then
            cprint("${yellow}warning: gentest codegen dependency cache could not be written")
            return false
        end
        -- The destination is a new immutable snapshot, so rename is atomic
        -- without replacing an existing usable cache file.
        local moved, move_errors = os.trymv(temporary, snapshot)
        if not moved then
            cprint("${yellow}warning: gentest codegen dependency cache could not be published: %s", move_errors or "unknown error")
        end
        return moved
    end)
    if not invoked or not ok_or_errors then
        os.tryrm(temporary)
        if not invoked then
            cprint("${yellow}warning: gentest codegen dependency cache was not updated: %s", ok_or_errors or "unknown error")
        end
        return
    end

    -- Keep the cache bounded while retaining the new valid snapshot. Failed
    -- cleanup is harmless; readers validate every snapshot before use.
    for _, candidate in ipairs(os.files(cache_path .. ".v.*") or {}) do
        if candidate ~= snapshot then
            os.tryrm(candidate)
        end
    end
    for _, stale_temporary in ipairs(os.files(cache_path .. ".next.*") or {}) do
        os.tryrm(stale_temporary)
    end
    return true
end

function with_cache_lock(cache_path, strict, callback)
    local directory = path.directory(cache_path)
    local prepared, prepare_errors = utils.trycall(function()
        os.mkdir(directory)
        return io.openlock(cache_path .. ".lock")
    end)
    if not prepared or not prepare_errors then
        local message = "gentest codegen dependency cache lock could not be opened: " .. tostring(prepare_errors or "unknown error")
        if strict then
            raise(message)
        end
        cprint("${yellow}warning: %s", message)
        return false
    end

    local lock = prepare_errors
    local locked, lock_errors = utils.trycall(function()
        lock:lock()
        return true
    end)
    if not locked or not lock_errors then
        utils.trycall(function()
            lock:close()
        end)
        local message = "gentest codegen dependency cache lock could not be acquired: " .. tostring(lock_errors or "unknown error")
        if strict then
            raise(message)
        end
        cprint("${yellow}warning: %s", message)
        return false
    end

    local invoked, result_or_errors = utils.trycall(callback)
    local unlocked, unlock_errors = utils.trycall(function()
        lock:unlock()
        return true
    end)
    local closed, close_errors = utils.trycall(function()
        lock:close()
        return true
    end)
    if not unlocked or not unlock_errors or not closed or not close_errors then
        local message = "gentest codegen dependency cache lock cleanup failed: " .. tostring(unlock_errors or close_errors or "unknown error")
        if strict then
            raise(message)
        end
        cprint("${yellow}warning: %s", message)
    end
    if not invoked then
        if strict then
            raise(result_or_errors or "gentest codegen dependency cache operation failed")
        end
        cprint("${yellow}warning: gentest codegen dependency cache was not updated: %s", result_or_errors or "unknown error")
        return false
    end
    return result_or_errors
end

function main(cache_path, depfile, identity, project_root, ...)
    local files = {}
    local seen = {}
    for _, filepath in ipairs(codegen_dep_cache_common.dependencies(depfile, project_root)) do
        append_unique(files, seen, filepath, project_root)
    end
    for _, filepath in ipairs({...}) do
        append_unique(files, seen, filepath, project_root)
    end
    with_cache_lock(cache_path, false, function()
        return save_snapshot_locked(cache_path, identity, files, {guards = {}, configured_roots = {}})
    end)
end
