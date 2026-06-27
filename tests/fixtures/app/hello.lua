local ok, err = pcall(function()
    Stream.new({
        start = function(controller)
            controller:close()
        end
    })
end)

assert(not ok)
assert(string.find(err, "request scope", 1, true) ~= nil)

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

        local stream_pulls = 0
        local stream = Stream.new({
            start = function(controller)
                controller:enqueue("start:")
            end,

            pull = function(controller)
                stream_pulls = stream_pulls + 1

                if stream_pulls == 1 then
                    controller:enqueue("pull")
                    return
                end

                controller:close()
            end
        })

        local stream_reader = stream:getReader()
        local stream_chunks = {}

        while true do
            local chunk = stream_reader:read()
            if chunk == nil then
                break
            end

            stream_chunks[#stream_chunks + 1] = chunk
        end

        file:write(table.concat(chunks))
        file:write("\n")
        file:write(table.concat(stream_chunks))
        file:close()
    end
}
