return {
    handler = function(r)
        local marker = assert(os.getenv("TEST_MARKER"))
        local file = assert(io.open(marker, "w"))
        file:write("ok")
        file:close()
    end
}
