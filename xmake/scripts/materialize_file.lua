local function die(message)
    error(message, 0)
end

function main(...)
    local argv = table.pack(...)
    local mode = argv[1]
    if mode == "template" then
        local template_file = argv[2]
        local output_file = argv[3]
        local text = io.readfile(template_file)
        if not text then
            die("failed to read template: " .. tostring(template_file))
        end
        for index = 4, #argv do
            local entry = tostring(argv[index] or "")
            local sep = entry:find("=", 1, true)
            if sep then
                local key = entry:sub(1, sep - 1)
                local value = entry:sub(sep + 1)
                text = text:gsub("%${" .. key .. "}", value)
            end
        end
        os.mkdir(path.directory(output_file))
        io.writefile(output_file, text)
        return
    end

    if mode == "copy" then
        local source_file = argv[2]
        local output_file = argv[3]
        local text = io.readfile(source_file)
        if text == nil then
            die("failed to read source file: " .. tostring(source_file))
        end
        os.mkdir(path.directory(output_file))
        io.writefile(output_file, text)
        return
    end

    if mode == "literal" then
        local output_file = argv[2]
        local text = tostring(argv[3] or "")
        os.mkdir(path.directory(output_file))
        io.writefile(output_file, text)
        return
    end

    die("unsupported materialize_file mode: " .. tostring(mode))
end
