return {
    handler = function(body)
        assert(type(body) == "userdata")

        local reader = body:getReader()
        local chunks = {}

        while true do
            local chunk = reader:read()
            if chunk == nil then
                break
            end

            chunks[#chunks + 1] = chunk
        end

        local marker = assert(os.getenv("TEST_MARKER"))
        local file = assert(io.open(marker, "w"))
        file:write(table.concat(chunks))
        file:close()
    end
}
