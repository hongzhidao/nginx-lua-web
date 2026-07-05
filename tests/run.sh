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

if "$NGINX" -V 2>&1 | grep -q -- '--with-http_ssl_module'; then
    HAVE_HTTP_SSL=1

else
    HAVE_HTTP_SSL=0
fi

PORT=${PORT:-$(python3 - <<'PY'
import socket

s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)}

UPSTREAM_PORT=${UPSTREAM_PORT:-$(python3 - "$PORT" <<'PY'
import socket
import sys

used = int(sys.argv[1])

while True:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()

    if port != used:
        print(port)
        break
PY
)}

HTTPS_UPSTREAM_PORT=${HTTPS_UPSTREAM_PORT:-$(python3 - "$PORT" "$UPSTREAM_PORT" <<'PY'
import socket
import sys

used = {int(arg) for arg in sys.argv[1:]}

while True:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()

    if port not in used:
        print(port)
        break
PY
)}

DNS_PORT=${DNS_PORT:-$(python3 - "$PORT" "$UPSTREAM_PORT" "$HTTPS_UPSTREAM_PORT" <<'PY'
import socket
import sys

used = {int(arg) for arg in sys.argv[1:]}

while True:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()

    if port not in used:
        print(port)
        break
PY
)}

rm -rf "$TEST_ROOT"
mkdir -p "$TEST_ROOT/conf" "$TEST_ROOT/logs" "$TEST_ROOT/client_body_temp"
mkdir -p "$TEST_ROOT/proxy_temp" "$TEST_ROOT/fastcgi_temp"
mkdir -p "$TEST_ROOT/uwsgi_temp" "$TEST_ROOT/scgi_temp"

: > "$TEST_ROOT/conf/https-upstream.conf"

if [ "$HAVE_HTTP_SSL" = 1 ]; then
    openssl req -x509 -newkey rsa:2048 -nodes \
        -keyout "$TEST_ROOT/conf/fetch.test.key" \
        -out "$TEST_ROOT/conf/fetch.test.crt" \
        -subj "/CN=fetch.test" -days 1 >/dev/null 2>&1

    cat > "$TEST_ROOT/conf/https-upstream.conf" <<EOF
    server {
        listen 127.0.0.1:$HTTPS_UPSTREAM_PORT ssl;
        ssl_certificate $TEST_ROOT/conf/fetch.test.crt;
        ssl_certificate_key $TEST_ROOT/conf/fetch.test.key;

        location /fetch-upstream {
            lua_web_file $TEST_ROOT/app-fetch-upstream.lua;
        }
    }
EOF
fi

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
        return Response.new({
            status = 500,
            body = text_stream("request method mismatch"),
        })
    end

    if not request.url:match("^http://127%.0%.0%.1:%d+/lua%-body$") then
        return Response.new({
            status = 500,
            body = text_stream("request url mismatch: " .. request.url),
        })
    end

    if request.headers == nil then
        return Response.new({
            status = 500,
            body = text_stream("request headers missing"),
        })
    end

    if request.body == nil then
        return Response.new({
            status = 500,
            body = text_stream("request body missing"),
        })
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

    return Response.new({
        status = 200,
        body = text_stream(table.concat(chunks)),
    })
end)

return app
EOF

cat > "$TEST_ROOT/app-request-url.lua" <<'EOF'
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
    return Response.new({
        status = 200,
        body = text_stream(request.url),
    })
end)

return app
EOF

cat > "$TEST_ROOT/app-request-new.lua" <<'EOF'
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
        local body = ReadableStream.new({
            start = function(controller)
                controller:enqueue("request body")
                controller:close()
            end,
        })

        Request.new()
        Request.new({})
        Request.new({ headers = { ["X-Test"] = "two" } })
        Request.new({ url = "https://example.test/path", method = "POST" })
        Request.new({ method = "POST", body = body })
        Request.new({
            url = "https://example.test/body",
            method = "POST",
            headers = { ["X-Test"] = "one" },
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
            headers = { ["X-Test"] = "one" },
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
        if with_body.body ~= body then
            error("request body stream mismatch")
        end

        local bad_default_body_ok = pcall(function()
            Request.new({ body = body })
        end)
        if bad_default_body_ok then
            error("Request.new accepted default GET body")
        end

        local bad_head_body_ok = pcall(function()
            Request.new({ method = "HEAD", body = body })
        end)
        if bad_head_body_ok then
            error("Request.new accepted HEAD body")
        end
    end)

    if not ok then
        return Response.new({
            status = 500,
            body = text_stream(err),
        })
    end

    return Response.new({
        status = 200,
        body = text_stream("Request.new"),
    })
end)

return app
EOF

cat > "$TEST_ROOT/app-request-no-body.lua" <<'EOF'
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
    if request.body ~= nil then
        return Response.new({
            status = 500,
            body = text_stream("request body should be nil"),
        })
    end

    return Response.new({
        status = 200,
        body = text_stream("request body nil"),
    })
end)

return app
EOF

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
    return Response.new({
        status = 201,
        headers = {
            ["Content-Type"] = "text/plain",
            ["X-Test"] = "one",
        },
        body = text_stream("hello from lua handler"),
    })
end)
return app
EOF

cat > "$TEST_ROOT/app-body-stream.lua" <<'EOF'
local app = App.new()

app:all("*", function(request)
    return Response.new({
        status = 200,
        body = request.body,
    })
end)

return app
EOF

cat > "$TEST_ROOT/app-response-new.lua" <<'EOF'
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
        local body = ReadableStream.new({
            start = function(controller)
                controller:enqueue("response body")
                controller:close()
            end,
        })

        Response.new()
        Response.new({})
        Response.new({ headers = { ["X-Test"] = "two" } })
        Response.new({ status = 201 })
        Response.new({ status = 204 })
        Response.new({ status = 205 })
        Response.new({ status = 304 })

        local empty = Response.new()
        if empty.status ~= 200 then
            error("default response status mismatch")
        end
        if empty.headers == nil then
            error("default response headers missing")
        end
        if empty.body ~= nil then
            error("default response body should be nil")
        end

        local with_body = Response.new({
            status = 202,
            headers = { ["X-Test"] = "one" },
            body = body,
        })
        if with_body.status ~= 202 then
            error("response status mismatch")
        end
        if with_body.headers == nil then
            error("response headers missing")
        end
        if with_body.body ~= body then
            error("response body stream mismatch")
        end

        local bad_status_ok = pcall(function()
            Response.new({ status = 99 })
        end)
        if bad_status_ok then
            error("Response.new accepted invalid status")
        end

        local bad_no_content_body_ok = pcall(function()
            Response.new({ status = 204, body = body })
        end)
        if bad_no_content_body_ok then
            error("Response.new accepted 204 body")
        end

        local bad_reset_content_body_ok = pcall(function()
            Response.new({ status = 205, body = body })
        end)
        if bad_reset_content_body_ok then
            error("Response.new accepted 205 body")
        end

        local bad_not_modified_body_ok = pcall(function()
            Response.new({ status = 304, body = body })
        end)
        if bad_not_modified_body_ok then
            error("Response.new accepted 304 body")
        end

        local bad_body_ok = pcall(function()
            Response.new({ body = "bad" })
        end)
        if bad_body_ok then
            error("Response.new accepted invalid body")
        end
    end)

    if not ok then
        return Response.new({
            status = 500,
            body = text_stream(err),
        })
    end

    return Response.new({
        status = 200,
        headers = { ["X-Response-Test"] = "ok" },
        body = text_stream("Response.new"),
    })
end)

return app
EOF

cat > "$TEST_ROOT/app-headers-new.lua" <<'EOF'
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

        Headers.new()
        Headers.new(headers)

        if headers:get("X-Test") ~= "one" then
            error("Headers.get value mismatch")
        end
        if headers:get("x-test") ~= "one" then
            error("Headers.get lowercase mismatch")
        end
        if headers:get("X-Other") ~= nil then
            error("Headers.get missing value mismatch")
        end

        local request = Request.new({ headers = headers })
        if request.headers == nil then
            error("request headers missing")
        end
        if request.headers == headers then
            error("request headers were not copied")
        end
        if request.headers:get("X-Test") ~= "one" then
            error("request headers get mismatch")
        end

        local response = Response.new({ headers = headers })
        if response.headers == nil then
            error("response headers missing")
        end
        if response.headers == headers then
            error("response headers were not copied")
        end
        if response.headers:get("X-Test") ~= "one" then
            error("response headers get mismatch")
        end
    end)

    if not ok then
        return Response.new({
            status = 500,
            body = text_stream(err),
        })
    end

    return Response.new({
        status = 200,
        body = text_stream("Headers.new"),
    })
end)

return app
EOF

cat > "$TEST_ROOT/app-url.lua" <<'EOF'
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local function expect(value, message)
    if not value then
        error(message)
    end
end

local app = App.new()

app:all("*", function()
    local ok, err = pcall(function()
        local url = URL.new("HTTP://Example.TEST:80/a/./b/../c?x=1&x=2#frag")

        expect(url.href == "http://example.test/a/c?x=1&x=2#frag",
               "URL href mismatch: " .. url.href)
        expect(tostring(url) == url.href, "URL tostring mismatch")
        expect(url:toString() == url.href, "URL toString mismatch")
        expect(url.origin == "http://example.test", "URL origin mismatch")
        expect(url.protocol == "http:", "URL protocol mismatch")
        expect(url.host == "example.test", "URL host mismatch")
        expect(url.hostname == "example.test", "URL hostname mismatch")
        expect(url.port == "", "URL port mismatch")
        expect(url.pathname == "/a/c", "URL pathname mismatch")
        expect(url.search == "?x=1&x=2", "URL search mismatch")
        expect(url.hash == "#frag", "URL hash mismatch")

        expect(url.searchParams:get("x") == "1",
               "URL searchParams first value mismatch")
        local all = url.searchParams:getAll("x")
        expect(#all == 2 and all[1] == "1" and all[2] == "2",
               "URL searchParams getAll mismatch")

        url.searchParams:append("sp ace", "a b")
        expect(url.search == "?x=1&x=2&sp+ace=a+b",
               "URL searchParams append did not sync search: " .. url.search)
        expect(url.href == "http://example.test/a/c?x=1&x=2&sp+ace=a+b#frag",
               "URL searchParams append did not sync href: " .. url.href)

        url.searchParams:set("x", "3")
        expect(url.searchParams.size == 2, "URLSearchParams size mismatch")
        expect(url.search == "?x=3&sp+ace=a+b",
               "URLSearchParams set mismatch: " .. url.search)

        url.searchParams:delete("sp ace", "a b")
        expect(url.search == "?x=3", "URLSearchParams delete mismatch")

        local relative = URL.new("../next?b=2",
                                 "http://example.test/dir/page?old=1#h")
        expect(relative.href == "http://example.test/next?b=2",
               "relative URL mismatch: " .. relative.href)

        local hash = URL.new("#new", relative)
        expect(hash.href == "http://example.test/next?b=2#new",
               "hash URL mismatch: " .. hash.href)

        local params = URLSearchParams.new({
            { "a", "1" },
            { "a", "2" },
            { "q", "a b" },
        })
        expect(params:toString() == "a=1&a=2&q=a+b",
               "URLSearchParams toString mismatch: " .. params:toString())
        expect(tostring(params) == params:toString(),
               "URLSearchParams tostring mismatch")
        expect(params:has("a", "2"), "URLSearchParams has mismatch")

        params:set("a", "3")
        expect(params:toString() == "a=3&q=a+b",
               "URLSearchParams set mismatch: " .. params:toString())

        params:delete("q", "a b")
        expect(params:toString() == "a=3",
               "URLSearchParams delete value mismatch: " .. params:toString())

        local copied = URLSearchParams.new(params)
        copied:append("z", "!")
        expect(copied:toString() == "a=3&z=%21",
               "URLSearchParams copy append mismatch: " .. copied:toString())
        expect(params:toString() == "a=3",
               "URLSearchParams copy shared storage")

        local bad_url_ok = pcall(function()
            URL.new("/relative")
        end)
        expect(not bad_url_ok, "URL.new accepted relative URL without base")

        local bad_base_ok = pcall(function()
            URL.new("relative", "not absolute")
        end)
        expect(not bad_base_ok, "URL.new accepted invalid base")
    end)

    if not ok then
        return Response.new({
            status = 500,
            body = text_stream(err),
        })
    end

    return Response.new({
        status = 200,
        body = text_stream("URL.new"),
    })
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
        return Response.new({
            status = 500,
            body = text_stream("Stream global is exposed"),
        })
    end

    local stream = ReadableStream.new({
        start = function(controller)
            controller:enqueue("hello ")
            controller:enqueue("from lua stream")
            controller:close()
        end,
    })

    if stream.enqueue ~= nil then
        return Response.new({
            status = 500,
            body = text_stream("ReadableStream exposes enqueue"),
        })
    end

    return Response.new({
        status = 200,
        body = stream,
    })
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

    return Response.new({
        status = 200,
        body = stream,
    })
end)

return app
EOF

cat > "$TEST_ROOT/app-fetch.lua" <<'EOF'
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local app = App.new()
local fetch_target = "http://fetch.test:__UPSTREAM_PORT__"
local https_fetch_url = "https://fetch.test:__HTTPS_UPSTREAM_PORT__/fetch-upstream"
local missing_target = "http://missing.test:__UPSTREAM_PORT__"

local function upstream_url(request)
    return request.url:gsub("/lua%-fetch.*$", "/fetch-upstream")
end

local function read_body(response)
    if response.body == nil then
        return ""
    end

    local reader = response.body:getReader()
    local chunks = {}

    while true do
        local result = reader:read()
        if result.done then
            break
        end

        chunks[#chunks + 1] = result.value
    end

    return table.concat(chunks)
end

app:all("*", function(request)
    if request.url:match("/lua%-fetch%-https$") then
        local body = ReadableStream.new({
            start = function(controller)
                controller:enqueue("init ")
                controller:enqueue("body")
                controller:close()
            end,
        })

        local response, err = fetch(https_fetch_url, {
            method = "POST",
            body = body,
        })

        if response == nil or response.status ~= 200 then
            return Response.new({
                status = 500,
                body = text_stream(err or "fetch HTTPS status mismatch"),
            })
        end

        if read_body(response) ~= "fetch init response" then
            return Response.new({
                status = 500,
                body = text_stream("fetch HTTPS body mismatch"),
            })
        end

        return Response.new({
            status = 200,
            body = text_stream("fetch HTTPS response"),
        })
    end

    if request.url:match("/lua%-fetch%-dns%-fail$") then
        local response, err = fetch(request, nil, { target = missing_target })

        if response ~= nil then
            return Response.new({
                status = 500,
                body = text_stream("fetch DNS failure returned response"),
            })
        end

        if err ~= "fetch DNS resolve failed" then
            return Response.new({
                status = 500,
                body = text_stream(err or "fetch DNS error missing"),
            })
        end

        return Response.new({
            status = 200,
            body = text_stream("fetch DNS resolve failed"),
        })
    end

    local url = upstream_url(request)
    local body = ReadableStream.new({
        start = function(controller)
            controller:enqueue("init ")
            controller:enqueue("body")
            controller:close()
        end,
    })

    local init_ok, init_err = fetch(url, {
        method = "POST",
        headers = {
            ["X-Test"] = "one",
        },
        body = body,
    })

    if init_ok == nil or init_ok.status ~= 200 then
        return Response.new({
            status = 500,
            body = text_stream(init_err or "fetch init status mismatch"),
        })
    end

    if read_body(init_ok) ~= "fetch init response" then
        return Response.new({
            status = 500,
            body = text_stream("fetch init body mismatch"),
        })
    end

    local response, err = fetch(request, nil, { target = fetch_target })

    if response == nil or response.status ~= 200 then
        return Response.new({
            status = 500,
            body = text_stream(err or "fetch response status mismatch"),
        })
    end

    return Response.new({
        status = response.status,
        body = response.body,
    })
end)

return app
EOF

sed -i "s/__UPSTREAM_PORT__/$UPSTREAM_PORT/g" "$TEST_ROOT/app-fetch.lua"
sed -i "s/__HTTPS_UPSTREAM_PORT__/$HTTPS_UPSTREAM_PORT/g" "$TEST_ROOT/app-fetch.lua"

cat > "$TEST_ROOT/app-fetch-body-after-yield.lua" <<'EOF'
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local function read_body(stream)
    local reader = stream:getReader()
    local chunks = {}

    while true do
        local result = reader:read()
        if result.done then
            break
        end

        chunks[#chunks + 1] = result.value
    end

    return table.concat(chunks)
end

local app = App.new()

app:all("*", function(request)
    local url = request.url:gsub("/lua%-fetch%-body%-after%-yield.*$",
                                 "/fetch-upstream")
    local response, err = fetch(url, {
        method = "POST",
        body = text_stream("yield before fetch body read"),
    })

    if response == nil or response.status ~= 200 then
        return Response.new({
            status = 500,
            body = text_stream(err or "fetch response status mismatch"),
        })
    end

    local request_body = read_body(request.body)
    local response_body = read_body(response.body)

    return Response.new({
        status = 200,
        body = text_stream(tostring(#response_body)
                           .. ":" .. response_body:sub(1, 3)
                           .. ":" .. request_body),
    })
end)

return app
EOF

cat > "$TEST_ROOT/app-fetch-no-body.lua" <<'EOF'
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
    local url = request.url:gsub("/lua%-fetch%-no%-body.*$",
                                 "/fetch-upstream")
    local response, err = fetch(url, {
        method = "POST",
        body = text_stream("fetch no body"),
    })

    if response == nil or response.status ~= 204 then
        return Response.new({
            status = 500,
            body = text_stream(err or "fetch no-body status mismatch"),
        })
    end

    if response.body ~= nil then
        return Response.new({
            status = 500,
            body = text_stream("fetch no-body response has body"),
        })
    end

    return Response.new({
        status = 200,
        body = text_stream("fetch response body nil"),
    })
end)

return app
EOF

cat > "$TEST_ROOT/app-fetch-head.lua" <<'EOF'
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
    local url = request.url:gsub("/lua%-fetch%-head.*$", "/fetch-upstream")
    local response, err = fetch(url, {
        method = "HEAD",
    })

    if response == nil or response.status ~= 200 then
        return Response.new({
            status = 500,
            body = text_stream(err or "fetch HEAD status mismatch"),
        })
    end

    if response.body ~= nil then
        return Response.new({
            status = 500,
            body = text_stream("fetch HEAD response has body"),
        })
    end

    return Response.new({
        status = 200,
        body = text_stream("fetch HEAD body nil"),
    })
end)

return app
EOF

cat > "$TEST_ROOT/app-fetch-upstream.lua" <<'EOF'
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
    local chunks = {}

    if request.method == "HEAD" then
        return Response.new({
            status = 200,
            headers = { ["X-Fetch-Upstream"] = "ok" },
            body = text_stream("fetch HEAD response body"),
        })
    end

    if request.body ~= nil then
        local reader = request.body:getReader()

        while true do
            local result = reader:read()
            if result.done then
                break
            end

            chunks[#chunks + 1] = result.value
        end
    end

    local body = table.concat(chunks)

    if body == "init body" then
        return Response.new({
            status = 200,
            headers = { ["X-Fetch-Upstream"] = "ok" },
            body = text_stream("fetch init response"),
        })
    end

    if body == "delayed fetch body" then
        return Response.new({
            status = 200,
            headers = { ["X-Fetch-Upstream"] = "ok" },
            body = text_stream("fetch request response"),
        })
    end

    if body == "yield before fetch body read" then
        return Response.new({
            status = 200,
            headers = { ["X-Fetch-Upstream"] = "ok" },
            body = text_stream(string.rep("x", 262144)),
        })
    end

    if body == "fetch no body" then
        return Response.new({
            status = 204,
            headers = { ["X-Fetch-Upstream"] = "ok" },
        })
    end

    return Response.new({
        status = 500,
        body = text_stream("bad fetch body: " .. body),
    })
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
    return Response.new({
        status = 202,
        body = text_stream("hello from second lua handler"),
    })
end)
return app
EOF

cat > "$TEST_ROOT/app-methods.lua" <<'EOF'
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local app = App.new()

app:get("/lua-methods", function(request)
    return Response.new({
        status = 200,
        body = text_stream("GET " .. request.method),
    })
end)

app:post("/lua-methods", function(request)
    return Response.new({
        status = 201,
        body = text_stream("POST " .. request.method),
    })
end)

app:post("/lua-methods-post-only", function(request)
    return Response.new({
        status = 202,
        body = text_stream("POST only " .. request.method),
    })
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
        return Response.new({
            status = 500,
            body = text_stream("App.new accepted an argument"),
        })
    end

    return Response.new({
        status = 203,
        body = text_stream("App.new rejected arguments"),
    })
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
        return Response.new({
            status = 500,
            body = text_stream("coroutine global is available"),
        })
    end

    if require_ok then
        return Response.new({
            status = 500,
            body = text_stream("coroutine module is available"),
        })
    end

    return Response.new({
        status = 200,
        body = text_stream("coroutine disabled"),
    })
end)

return app
EOF

cat > "$TEST_ROOT/conf/nginx.conf" <<EOF
worker_processes  1;
error_log  logs/error.log notice;
pid        logs/nginx.pid;

events {
    worker_connections  64;
}

http {
    access_log off;
    resolver 127.0.0.1:$DNS_PORT ipv6=off;
    resolver_timeout 1s;

    server {
        listen 127.0.0.1:$PORT;

        location /lua-body {
            lua_web_file $TEST_ROOT/app-body.lua;
        }

        location /lua-request-url {
            lua_web_file $TEST_ROOT/app-request-url.lua;
        }

        location /lua-request-new {
            lua_web_file $TEST_ROOT/app-request-new.lua;
        }

        location /lua-request-no-body {
            lua_web_file $TEST_ROOT/app-request-no-body.lua;
        }

        location /lua {
            lua_web_file $TEST_ROOT/app.lua;
        }

        location /lua-body-stream {
            lua_web_file $TEST_ROOT/app-body-stream.lua;
        }

        location /lua-response-new {
            lua_web_file $TEST_ROOT/app-response-new.lua;
        }

        location /lua-headers-new {
            lua_web_file $TEST_ROOT/app-headers-new.lua;
        }

        location /lua-url {
            lua_web_file $TEST_ROOT/app-url.lua;
        }

        location /lua-stream {
            lua_web_file $TEST_ROOT/app-stream.lua;
        }

        location /lua-stream-pull {
            lua_web_file $TEST_ROOT/app-stream-pull.lua;
        }

        location /lua-fetch {
            lua_web_file $TEST_ROOT/app-fetch.lua;
        }

        location /lua-fetch-dns-fail {
            lua_web_file $TEST_ROOT/app-fetch.lua;
        }

        location /lua-fetch-https {
            lua_web_file $TEST_ROOT/app-fetch.lua;
        }

        location /lua-fetch-body-after-yield {
            lua_web_file $TEST_ROOT/app-fetch-body-after-yield.lua;
        }

        location /lua-fetch-no-body {
            lua_web_file $TEST_ROOT/app-fetch-no-body.lua;
        }

        location /lua-fetch-head {
            lua_web_file $TEST_ROOT/app-fetch-head.lua;
        }

        location /fetch-upstream {
            lua_web_file $TEST_ROOT/app-fetch-upstream.lua;
        }

        location /lua-alt {
            lua_web_file $TEST_ROOT/app-alt.lua;
        }

        location /lua-methods {
            lua_web_file $TEST_ROOT/app-methods.lua;
        }

        location /lua-app-args {
            lua_web_file $TEST_ROOT/app-args.lua;
        }

        location /lua-coroutine-disabled {
            lua_web_file $TEST_ROOT/app-coroutine-disabled.lua;
        }
    }

    server {
        listen 127.0.0.1:$UPSTREAM_PORT;

        location /lua-fetch {
            lua_web_file $TEST_ROOT/app-fetch-upstream.lua;
        }
    }

    include $TEST_ROOT/conf/https-upstream.conf;
}
EOF

DNS_PID=

python3 - "$DNS_PORT" <<'PY' &
import socket
import struct
import sys

port = int(sys.argv[1])
server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
server.bind(("127.0.0.1", port))

while True:
    data, addr = server.recvfrom(512)

    if len(data) < 12:
        continue

    offset = 12
    labels = []

    while offset < len(data):
        size = data[offset]
        offset += 1

        if size == 0:
            break

        if offset + size > len(data):
            break

        labels.append(data[offset:offset + size])
        offset += size

    if offset + 4 > len(data):
        continue

    qtype, qclass = struct.unpack("!HH", data[offset:offset + 4])
    question = data[12:offset + 4]
    name = b".".join(labels).lower()
    found = name == b"fetch.test" and qtype == 1 and qclass == 1

    flags = 0x8180 if found else 0x8183
    answer_count = 1 if found else 0
    response = data[:2] + struct.pack("!HHHHH", flags, 1, answer_count, 0, 0)
    response += question

    if found:
        response += b"\xc0\x0c"
        response += struct.pack("!HHIH", 1, 1, 60, 4)
        response += socket.inet_aton("127.0.0.1")

    server.sendto(response, addr)
PY
DNS_PID=$!

cleanup() {
    "$NGINX" -p "$TEST_ROOT/" -c conf/nginx.conf -s stop >/dev/null 2>&1 || true
    if [ -n "$DNS_PID" ]; then
        kill "$DNS_PID" >/dev/null 2>&1 || true
    fi
}

trap cleanup EXIT INT TERM

if ! "$NGINX" -p "$TEST_ROOT/" -c conf/nginx.conf -t >"$TEST_ROOT/nginx-test.log" 2>&1; then
    cat "$TEST_ROOT/nginx-test.log" >&2
    exit 1
fi

"$NGINX" -p "$TEST_ROOT/" -c conf/nginx.conf

for test_file in \
    test_requests.py \
    test_response.py \
    test_headers.py \
    test_url.py \
    test_stream.py \
    test_fetch.py \
    test_app.py
do
    TEST_NGINX_PORT="$PORT" \
    TEST_NGINX_ROOT="$TEST_ROOT" \
    TEST_NGINX_HAVE_HTTP_SSL="$HAVE_HTTP_SSL" \
        python3 "$MODULE_DIR/tests/$test_file"
done
