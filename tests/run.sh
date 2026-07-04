#!/bin/sh

set -eu

MODULE_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
NGINX_DIR=${NGINX_DIR:-"$MODULE_DIR/../nginx"}
DEFAULT_NGINX="$NGINX_DIR/objs/nginx"
NGINX=${NGINX:-"$DEFAULT_NGINX"}
TEST_ROOT=${TEST_ROOT:-/tmp/nginx-lua-web-test.$$}

if [ ! -x "$NGINX" ]; then
    echo "nginx test binary not found: $NGINX" >&2
    echo "set NGINX=/path/to/nginx, or build nginx with this module first" >&2
    exit 1
fi

if [ "$NGINX" = "$DEFAULT_NGINX" ]; then
    if [ ! -f "$NGINX_DIR/objs/ngx_modules.c" ] \
        || ! grep -q 'ngx_http_lua_module' "$NGINX_DIR/objs/ngx_modules.c"; then
        echo "nginx test binary was not built with ngx_http_lua_module" >&2
        echo "set NGINX=/path/to/nginx, or build nginx with this module first" >&2
        exit 1
    fi
fi

PORT=${PORT:-$(python3 - <<'PY'
import socket

s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)}

rm -rf "$TEST_ROOT"
mkdir -p "$TEST_ROOT/conf" "$TEST_ROOT/logs" "$TEST_ROOT/client_body_temp"
mkdir -p "$TEST_ROOT/proxy_temp" "$TEST_ROOT/fastcgi_temp"
mkdir -p "$TEST_ROOT/uwsgi_temp" "$TEST_ROOT/scgi_temp"

cat > "$TEST_ROOT/app.lua" <<'EOF'
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local app = App.new()
app:all("*", function()
    return { 201, text_stream("hello from lua handler") }
end)
return app
EOF

cat > "$TEST_ROOT/app-alt.lua" <<'EOF'
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local app = App.new()
app:all("/lua-alt", function()
    return { 202, text_stream("hello from second lua handler") }
end)
return app
EOF

cat > "$TEST_ROOT/app-args.lua" <<'EOF'
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local ok = pcall(function()
    App.new("bad")
end)

local app = App.new()

app:all("*", function()
    if ok then
        return { 500, text_stream("App.new accepted an argument") }
    end

    return { 203, text_stream("App.new rejected arguments") }
end)

return app
EOF

cat > "$TEST_ROOT/app-coroutine-disabled.lua" <<'EOF'
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local app = App.new()
local require_ok = pcall(require, "coroutine")

app:all("*", function()
    if coroutine ~= nil then
        return { 500, text_stream("coroutine global is available") }
    end

    if require_ok then
        return { 500, text_stream("coroutine module is available") }
    end

    return { 200, text_stream("coroutine disabled") }
end)

return app
EOF

cat > "$TEST_ROOT/app-body.lua" <<'EOF'
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local app = App.new()

app:all("*", function(request)
    if request.method ~= "POST" then
        return { 500, text_stream("request method mismatch") }
    end

    if request.url ~= "/lua-body" then
        return { 500, text_stream("request url mismatch") }
    end

    if request.headers == nil then
        return { 500, text_stream("request headers missing") }
    end

    if request.body == nil then
        return { 500, text_stream("request body missing") }
    end

    local reader = request.body:getReader()
    local chunks = {}

    while true do
        local result = reader:read()
        if result.done then
            break
        end

        chunks[#chunks + 1] = result.value
    end

    return { 200, text_stream(table.concat(chunks)) }
end)

return app
EOF

cat > "$TEST_ROOT/app-body-stream.lua" <<'EOF'
local app = App.new()

app:all("*", function(request)
    return { 200, request.body }
end)

return app
EOF

cat > "$TEST_ROOT/app-stream.lua" <<'EOF'
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local app = App.new()

app:all("*", function()
    if Stream ~= nil then
        return { 500, text_stream("Stream global is exposed") }
    end

    local stream = ReadableStream.new({
        start = function(controller)
            controller:enqueue("hello ")
            controller:enqueue("from lua stream")
            controller:close()
        end,
    })

    if stream.enqueue ~= nil then
        return { 500, text_stream("ReadableStream exposes enqueue") }
    end

    return { 200, stream }
end)

return app
EOF

cat > "$TEST_ROOT/app-stream-pull.lua" <<'EOF'
local app = App.new()

app:all("*", function()
    local pulls = 0

    local stream = ReadableStream.new({
        pull = function(controller)
            pulls = pulls + 1

            if pulls == 1 then
                controller:enqueue("pulled ")
                return
            end

            controller:enqueue("from source")
            controller:close()
        end,
    })

    return { 200, stream }
end)

return app
EOF

cat > "$TEST_ROOT/app-request-headers-new.lua" <<'EOF'
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local app = App.new()

app:all("*", function()
    local ok, err = pcall(function()
        local headers = Headers.new({ ["X-Test"] = "one" })
        local body = ReadableStream.new({
            start = function(controller)
                controller:enqueue("request body")
                controller:close()
            end,
        })

        Headers.new()
        Headers.new(headers)
        Request.new()
        Request.new({})
        Request.new({ headers = headers })
        Request.new({ headers = { ["X-Test"] = "two" } })
        Request.new({ url = "https://example.test/path", method = "POST" })
        Request.new({
            url = "https://example.test/headers",
            method = "PUT",
            headers = headers,
        })
        Request.new({ method = "POST", body = body })
        Request.new({
            url = "https://example.test/body",
            method = "POST",
            headers = headers,
            body = body,
        })

        local empty = Request.new()
        if empty.url ~= "" then
            error("default request url mismatch")
        end
        if empty.method ~= "GET" then
            error("default request method mismatch")
        end
        if empty.headers == nil then
            error("default request headers missing")
        end
        if empty.body ~= nil then
            error("default request body should be nil")
        end

        local with_body = Request.new({
            url = "https://example.test/body",
            method = "POST",
            headers = headers,
            body = body,
        })
        if with_body.url ~= "https://example.test/body" then
            error("request url mismatch")
        end
        if with_body.method ~= "POST" then
            error("request method mismatch")
        end
        if with_body.headers == nil then
            error("request headers missing")
        end
        if with_body.headers == headers then
            error("request headers were not copied")
        end
        if with_body.body ~= body then
            error("request body stream mismatch")
        end
    end)

    if not ok then
        return { 500, text_stream(err) }
    end

    return { 200, text_stream("Request.new and Headers.new") }
end)

return app
EOF

cat > "$TEST_ROOT/conf/nginx.conf" <<EOF
worker_processes  1;
error_log  logs/error.log notice;
pid        logs/nginx.pid;

events {
    worker_connections  16;
}

http {
    access_log off;

    server {
        listen 127.0.0.1:$PORT;

        location /lua {
            lua_web_file $TEST_ROOT/app.lua;
        }

        location /lua-alt {
            lua_web_file $TEST_ROOT/app-alt.lua;
        }

        location /lua-app-args {
            lua_web_file $TEST_ROOT/app-args.lua;
        }

        location /lua-coroutine-disabled {
            lua_web_file $TEST_ROOT/app-coroutine-disabled.lua;
        }

        location /lua-body {
            lua_web_file $TEST_ROOT/app-body.lua;
        }

        location /lua-body-stream {
            lua_web_file $TEST_ROOT/app-body-stream.lua;
        }

        location /lua-stream {
            lua_web_file $TEST_ROOT/app-stream.lua;
        }

        location /lua-stream-pull {
            lua_web_file $TEST_ROOT/app-stream-pull.lua;
        }

        location /lua-request-headers-new {
            lua_web_file $TEST_ROOT/app-request-headers-new.lua;
        }
    }
}
EOF

cleanup() {
    "$NGINX" -p "$TEST_ROOT/" -c conf/nginx.conf -s stop >/dev/null 2>&1 || true
}

trap cleanup EXIT INT TERM

if ! "$NGINX" -p "$TEST_ROOT/" -c conf/nginx.conf -t >"$TEST_ROOT/nginx-test.log" 2>&1; then
    cat "$TEST_ROOT/nginx-test.log" >&2
    exit 1
fi

"$NGINX" -p "$TEST_ROOT/" -c conf/nginx.conf

TEST_NGINX_PORT="$PORT" python3 "$MODULE_DIR/tests/test_lua_web.py"
