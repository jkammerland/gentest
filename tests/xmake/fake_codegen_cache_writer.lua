function main(output, depfile, identity, delay_ms)
    local delay = tonumber(delay_ms) or 0
    if delay > 0 then
        os.sleep(delay)
    end
    io.writefile(output, identity)
    io.writefile(depfile, "output: " .. output .. "\n")
end
