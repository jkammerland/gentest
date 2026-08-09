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
        table.insert(fingerprint, entry.path .. "=" .. tostring(entry.mtime))
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
    if not loaded or type(state) ~= "table" or state.schema ~= 1 or state.identity ~= identity or type(state.files) ~= "table" or
        #state.files ~= #entries then
        return false
    end
    for index, entry in ipairs(entries) do
        local cached = state.files[index]
        if type(cached) ~= "table" or cached.path ~= entry.path or cached.mtime ~= entry.mtime then
            return false
        end
    end
    return true
end

local function save_snapshot_locked(cache_path, identity, files)
    local entries = {}
    for _, filepath in ipairs(files) do
        table.insert(entries, {path = filepath, mtime = os.mtime(filepath)})
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
        io.save(temporary, {schema = 1, identity = identity, files = entries}, {orderkeys = true})
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
end

local function save_snapshot(cache_path, identity, files)
    local directory = path.directory(cache_path)
    local prepared, prepare_errors = utils.trycall(function()
        os.mkdir(directory)
        return io.openlock(cache_path .. ".lock")
    end)
    if not prepared or not prepare_errors then
        cprint("${yellow}warning: gentest codegen dependency cache lock could not be opened: %s", prepare_errors or "unknown error")
        return
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
        cprint("${yellow}warning: gentest codegen dependency cache lock could not be acquired: %s", lock_errors or "unknown error")
        return
    end

    local saved, save_errors = utils.trycall(function()
        save_snapshot_locked(cache_path, identity, files)
        return true
    end)
    local unlocked, unlock_errors = utils.trycall(function()
        lock:unlock()
        return true
    end)
    local closed, close_errors = utils.trycall(function()
        lock:close()
        return true
    end)
    if not saved then
        cprint("${yellow}warning: gentest codegen dependency cache was not updated: %s", save_errors or "unknown error")
    elseif not unlocked or not unlock_errors or not closed or not close_errors then
        cprint("${yellow}warning: gentest codegen dependency cache lock cleanup failed: %s", unlock_errors or close_errors or "unknown error")
    end
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
    save_snapshot(cache_path, identity, files)
end
