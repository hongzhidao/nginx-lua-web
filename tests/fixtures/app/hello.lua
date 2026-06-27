return {
    handler = function(body)
        assert(type(body) == "userdata")

        local marker = assert(os.getenv("TEST_MARKER"))
        local file = assert(io.open(marker, "w"))
        file:write("ok")
        file:close()
    end
}
