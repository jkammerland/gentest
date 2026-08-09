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

local function is_make_escape(character)
    return character == " " or character == "\t" or character == "#" or character == "$" or character == ":" or character == "\\"
end

local function collapse_continuations(contents)
    local result = {}
    local index = 1
    while index <= #contents do
        if contents:sub(index, index) ~= "\\" then
            table.insert(result, contents:sub(index, index))
            index = index + 1
        else
            local run_end = index
            while run_end <= #contents and contents:sub(run_end, run_end) == "\\" do
                run_end = run_end + 1
            end
            local count = run_end - index
            local newline_width = 0
            if contents:sub(run_end, run_end + 1) == "\r\n" then
                newline_width = 2
            elseif contents:sub(run_end, run_end) == "\n" then
                newline_width = 1
            end
            if newline_width > 0 and count % 2 == 1 then
                -- Only an odd final backslash escapes the newline. Preserve
                -- every preceding pair for the Make-token decoder below.
                table.insert(result, string.rep("\\", count - 1))
                table.insert(result, " ")
                index = run_end + newline_width
            else
                table.insert(result, string.rep("\\", count))
                index = run_end
            end
        end
    end
    return table.concat(result)
end

function dependencies(depfile, project_root)
    if not depfile or depfile == "" or not os.isfile(depfile) then
        return {}
    end
    local contents = io.readfile(depfile)
    if not contents then
        return {}
    end
    contents = collapse_continuations(contents)

    -- A drive-letter colon is not the make-rule separator. The generator
    -- writes a separator followed by whitespace, which is portable for both
    -- native Windows and POSIX depfiles.
    local separator = nil
    local index = 1
    while index <= #contents do
        local ch = contents:sub(index, index)
        local next_ch = contents:sub(index + 1, index + 1)
        if ch == "\\" and is_make_escape(next_ch) then
            index = index + 2
        elseif ch == ":" and next_ch:match("%s") then
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
        local next_ch = contents:sub(index + 1, index + 1)
        if ch == "\\" and index < #contents and is_make_escape(next_ch) then
            table.insert(token, next_ch)
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
