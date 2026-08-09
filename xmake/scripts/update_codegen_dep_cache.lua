-- Persist the dependency closure discovered by gentest_codegen. This runs
-- after codegen as a batch command, so a failed generator never replaces a
-- usable cache snapshot.

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

local function depfile_dependencies(depfile, project_root)
    if not depfile or depfile == "" or not os.isfile(depfile) then
        return {}
    end
    local contents = io.readfile(depfile)
    if not contents then
        return {}
    end
    contents = contents:gsub("\\\r\n", " "):gsub("\\\n", " ")

    -- A drive-letter colon is not the make-rule separator. The generator
    -- writes a separator followed by whitespace, which is portable for both
    -- native Windows and POSIX depfiles.
    local separator = nil
    local index = 1
    while index <= #contents do
        local ch = contents:sub(index, index)
        if ch == "\\" then
            index = index + 2
        elseif ch == ":" and contents:sub(index + 1, index + 1):match("%s") then
            separator = index
            break
        else
            index = index + 1
        end
    end
    if not separator then
        return {}
    end

    local result = {}
    local seen = {}
    local token = {}
    local function flush()
        if #token > 0 then
            append_unique(result, seen, table.concat(token), project_root)
            token = {}
        end
    end
    index = separator + 1
    while index <= #contents do
        local ch = contents:sub(index, index)
        if ch == "\\" and index < #contents then
            table.insert(token, contents:sub(index + 1, index + 1))
            index = index + 2
        elseif ch == " " or ch == "\t" or ch == "\r" or ch == "\n" then
            flush()
            index = index + 1
        else
            table.insert(token, ch)
            index = index + 1
        end
    end
    flush()
    return result
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

local function save_snapshot(cache_path, identity, files)
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

    local temporary = cache_path .. ".next"
    os.tryrm(temporary)
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
end

function main(cache_path, depfile, identity, project_root, ...)
    local files = {}
    local seen = {}
    for _, filepath in ipairs(depfile_dependencies(depfile, project_root)) do
        append_unique(files, seen, filepath, project_root)
    end
    for _, filepath in ipairs({...}) do
        append_unique(files, seen, filepath, project_root)
    end
    save_snapshot(cache_path, identity, files)
end
