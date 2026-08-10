function main(output, depfile, identity, delay_ms, lookup_sidecar, lookup_state, invocation_count)
    local delay = tonumber(delay_ms) or 0
    if delay > 0 then
        os.sleep(delay)
    end
    io.writefile(output, identity)
    io.writefile(depfile, "output: " .. output .. "\n")
    if invocation_count and invocation_count ~= "" then
        local count = 0
        if os.isfile(invocation_count) then
            count = tonumber(io.readfile(invocation_count)) or 0
        end
        io.writefile(invocation_count, tostring(count + 1))
    end
    if lookup_sidecar and lookup_sidecar ~= "" then
        if lookup_state == "corrupt" then
            io.writefile(lookup_sidecar, "{not-json")
        elseif lookup_state == "incomplete" then
            io.writefile(
                lookup_sidecar,
                '{"schema":"gentest.lookup_guards.v2","complete":false,"guards":[],"configured_roots":[]}\n'
            )
        else
            io.writefile(
                lookup_sidecar,
                '{"schema":"gentest.lookup_guards.v2","complete":true,"guards":[],"configured_roots":[]}\n'
            )
        end
    end
end
