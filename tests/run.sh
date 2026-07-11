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

SLOW_FETCH_PORT=${SLOW_FETCH_PORT:-$(python3 - "$PORT" "$UPSTREAM_PORT" "$HTTPS_UPSTREAM_PORT" <<'PY'
import socket
import sys

used = {int(arg) for arg in sys.argv[1:]}

while True:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()

    if port not in used:
        print(port)
        break
PY
)}

HEADER_FETCH_PORT=${HEADER_FETCH_PORT:-$(python3 - "$PORT" "$UPSTREAM_PORT" "$HTTPS_UPSTREAM_PORT" "$SLOW_FETCH_PORT" <<'PY'
import socket
import sys

used = {int(arg) for arg in sys.argv[1:]}

while True:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()

    if port not in used:
        print(port)
        break
PY
)}

FETCH_UPLOAD_PORT=${FETCH_UPLOAD_PORT:-$(python3 - "$PORT" "$UPSTREAM_PORT" "$HTTPS_UPSTREAM_PORT" "$SLOW_FETCH_PORT" "$HEADER_FETCH_PORT" <<'PY'
import socket
import sys

used = {int(arg) for arg in sys.argv[1:]}

while True:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()

    if port not in used:
        print(port)
        break
PY
)}

DNS_PORT=${DNS_PORT:-$(python3 - "$PORT" "$UPSTREAM_PORT" "$HTTPS_UPSTREAM_PORT" "$SLOW_FETCH_PORT" "$HEADER_FETCH_PORT" "$FETCH_UPLOAD_PORT" <<'PY'
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
    if request.url:match("/lua%-body%-text$") then
        if request.bodyUsed then
            return Response.new({
                status = 500,
                body = text_stream("request text body should start unused"),
            })
        end

        local text = request:text()

        if not request.bodyUsed then
            return Response.new({
                status = 500,
                body = text_stream("request text bodyUsed missing"),
            })
        end

        local second_ok = pcall(function()
            request:text()
        end)
        if second_ok then
            return Response.new({
                status = 500,
                body = text_stream("request text allowed second consume"),
            })
        end

        return Response.new({
            status = 200,
            body = text_stream(text),
        })
    end

    if request.url:match("/lua%-body%-json$") then
        local data = request:json()

        if data.ok ~= true or data.count ~= 2 or data.message ~= "hello" then
            return Response.new({
                status = 500,
                body = text_stream("request json mismatch"),
            })
        end

        if not request.bodyUsed then
            return Response.new({
                status = 500,
                body = text_stream("request json bodyUsed missing"),
            })
        end

        return Response.json({
            ok = data.ok,
            count = data.count,
            message = data.message,
        })
    end

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

    if request.bodyUsed then
        return Response.new({
            status = 500,
            body = text_stream("request body should start unused"),
        })
    end

    local reader = request.body:getReader()

    if request.bodyUsed then
        return Response.new({
            status = 500,
            body = text_stream("request body used before read"),
        })
    end

    local chunks = {}

    while true do
        local result = reader:read()
        if not request.bodyUsed then
            return Response.new({
                status = 500,
                body = text_stream("request body used missing after read"),
            })
        end

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

cat > "$TEST_ROOT/app-request-headers.lua" <<'EOF'
local app = App.new()

app:all("*", function(request)
    return Response.json({
        authorization = request.headers:get("authorization"),
        cookie = request.headers:get("COOKIE"),
        content_type = request.headers:get("cOnTeNt-TyPe"),
        custom = request.headers:get("X-Incoming-Test"),
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
        Request.new("https://example.test/path")
        Request.new("https://example.test/path", {
            headers = { ["X-Test"] = "two" },
        })
        Request.new("https://example.test/path", { method = "POST" })
        Request.new("https://example.test/body", {
            method = "POST",
            body = body,
        })
        Request.new("https://example.test/body", {
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

        local with_body = Request.new("https://example.test/body", {
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

        local from_url = Request.new("https://example.test/from-input", {
            url = "https://bad.example/ignored",
            method = "POST",
            headers = { ["X-Test"] = "from-url" },
            body = body,
        })
        if from_url.url ~= "https://example.test/from-input" then
            error("Request.new string input url mismatch")
        end
        if from_url.method ~= "POST" then
            error("Request.new string input method mismatch")
        end
        if from_url.headers:get("X-Test") ~= "from-url" then
            error("Request.new string input headers mismatch")
        end
        if from_url.body ~= body then
            error("Request.new string input body mismatch")
        end

        local copied = Request.new(with_body)
        if copied == with_body then
            error("Request.new Request input reused object")
        end
        if copied.url ~= with_body.url then
            error("Request.new Request input url mismatch")
        end
        if copied.method ~= with_body.method then
            error("Request.new Request input method mismatch")
        end
        if copied.headers == with_body.headers then
            error("Request.new Request input shared headers")
        end
        if copied.headers:get("X-Test") ~= "one" then
            error("Request.new Request input headers mismatch")
        end
        if copied.body ~= with_body.body then
            error("Request.new Request input body mismatch")
        end

        local copied_empty_init = Request.new(with_body, {})
        if copied_empty_init == with_body then
            error("Request.new Request empty init reused object")
        end
        if copied_empty_init.url ~= with_body.url then
            error("Request.new Request empty init url mismatch")
        end
        if copied_empty_init.headers == with_body.headers then
            error("Request.new Request empty init shared headers")
        end
        if copied_empty_init.headers:get("X-Test") ~= "one" then
            error("Request.new Request empty init headers mismatch")
        end

        local header_source = Request.new("https://example.test/headers", {
            headers = {
                ["X-Test"] = "one",
                ["X-Old"] = "old",
            },
        })
        local overridden = Request.new(header_source, {
            url = "https://bad.example/ignored",
            headers = { ["X-Test"] = "two" },
        })
        if overridden.url ~= header_source.url then
            error("Request.new Request init changed url")
        end
        if overridden.headers:get("X-Test") ~= "two" then
            error("Request.new Request init headers mismatch")
        end
        if overridden.headers:get("X-Old") ~= nil then
            error("Request.new Request init merged old header")
        end
        if header_source.headers:get("X-Test") ~= "one"
            or header_source.headers:get("X-Old") ~= "old"
        then
            error("Request.new mutated input Request")
        end

        local bad_table_input_ok = pcall(function()
            Request.new({ url = "https://example.test/table" })
        end)
        if bad_table_input_ok then
            error("Request.new accepted table input")
        end

        local bad_table_input_init_ok = pcall(function()
            Request.new({ url = "https://example.test/table" }, {})
        end)
        if bad_table_input_init_ok then
            error("Request.new accepted table input with init")
        end

        local bad_nil_input_init_ok = pcall(function()
            Request.new(nil, {})
        end)
        if bad_nil_input_init_ok then
            error("Request.new accepted nil input with init")
        end

        local bad_default_body_ok = pcall(function()
            Request.new("https://example.test/body", { body = body })
        end)
        if bad_default_body_ok then
            error("Request.new accepted default GET body")
        end

        local bad_head_body_ok = pcall(function()
            Request.new("https://example.test/body", {
                method = "HEAD",
                body = body,
            })
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

    if request.bodyUsed then
        return Response.new({
            status = 500,
            body = text_stream("request bodyUsed should be false without body"),
        })
    end

    if request:text() ~= "" then
        return Response.new({
            status = 500,
            body = text_stream("request text should be empty without body"),
        })
    end

    if request.bodyUsed then
        return Response.new({
            status = 500,
            body = text_stream("request bodyUsed changed without body"),
        })
    end

    local bad_json_ok = pcall(function()
        request:json()
    end)
    if bad_json_ok then
        return Response.new({
            status = 500,
            body = text_stream("request json accepted empty body"),
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
        if empty.bodyUsed then
            error("default response bodyUsed should be false")
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
        if with_body.bodyUsed then
            error("response bodyUsed should be false before read")
        end

        local reader = with_body.body:getReader()
        while true do
            local result = reader:read()
            if result.done then
                break
            end
        end

        if not with_body.bodyUsed then
            error("response bodyUsed should be true after read")
        end

        local text_response = Response.new({
            body = text_stream("response text"),
        })
        if text_response:text() ~= "response text" then
            error("Response:text body mismatch")
        end
        if not text_response.bodyUsed then
            error("Response:text bodyUsed missing")
        end

        local second_text_ok = pcall(function()
            text_response:text()
        end)
        if second_text_ok then
            error("Response:text accepted used body")
        end

        local json_response = Response.json({
            ok = true,
            count = 2,
            message = "response json",
        })
        local json_data = json_response:json()
        if json_data.ok ~= true
            or json_data.count ~= 2
            or json_data.message ~= "response json"
        then
            error("Response:json body mismatch")
        end
        if not json_response.bodyUsed then
            error("Response:json bodyUsed missing")
        end

        local empty_response = Response.new()
        if empty_response:text() ~= "" then
            error("Response:text empty body mismatch")
        end
        if empty_response.bodyUsed then
            error("Response:text changed bodyUsed without body")
        end

        local empty_json_ok = pcall(function()
            Response.new():json()
        end)
        if empty_json_ok then
            error("Response:json accepted empty body")
        end

        local locked_response = Response.new({
            body = text_stream("locked body"),
        })
        local locked_reader = locked_response.body:getReader()
        local locked_text_ok = pcall(function()
            locked_response:text()
        end)
        if locked_text_ok then
            error("Response:text accepted locked body")
        end
        locked_reader:releaseLock()

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

        local function expect_bad_response_headers(headers, message)
            local ok = pcall(function()
                Response.new({ headers = headers })
            end)
            if ok then
                error(message)
            end
        end

        expect_bad_response_headers(
            { [""] = "bad" },
            "Response.new accepted empty header name")
        expect_bad_response_headers(
            { ["Bad Header"] = "bad" },
            "Response.new accepted invalid header name")
        expect_bad_response_headers(
            { ["Bad:Header"] = "bad" },
            "Response.new accepted header name with colon")
        expect_bad_response_headers(
            { ["X-Bad"] = "one\r\ntwo" },
            "Response.new accepted response-splitting header value")
        expect_bad_response_headers(
            { ["X-Bad"] = "one\0two" },
            "Response.new accepted NUL header value")
        expect_bad_response_headers(
            { ["X-Bad"] = "one\127two" },
            "Response.new accepted DEL header value")
    end)

    if not ok then
        return Response.new({
            status = 500,
            body = text_stream(err),
        })
    end

    local sent_response
    local sent_body = ReadableStream.new({
        pull = function(controller)
            if not sent_response.bodyUsed then
                controller:enqueue("response bodyUsed should be true during send")
                controller:close()
                return
            end

            controller:enqueue("Response.new")
            controller:close()
        end,
    })

    sent_response = Response.new({
        status = 200,
        headers = { ["X-Response-Test"] = "ok" },
        body = sent_body,
    })

    return sent_response
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

        local set_headers = Headers.new({ ["X-Test"] = "one" })

        set_headers:set("X-Test", "two")
        if set_headers:get("X-Test") ~= "two" then
            error("Headers.set replace mismatch")
        end
        if set_headers:get("x-test") ~= "two" then
            error("Headers.set lowercase replace mismatch")
        end

        set_headers:set("X-Other", "three")
        if set_headers:get("x-other") ~= "three" then
            error("Headers.set new value mismatch")
        end

        set_headers:set("x-test", "four")
        if set_headers:get("X-Test") ~= "four" then
            error("Headers.set case-insensitive replace mismatch")
        end

        local bad_name_ok = pcall(function()
            set_headers:set(1, "bad")
        end)
        if bad_name_ok then
            error("Headers.set accepted invalid name")
        end

        local bad_value_ok = pcall(function()
            set_headers:set("X-Bad", {})
        end)
        if bad_value_ok then
            error("Headers.set accepted invalid value")
        end

        local bad_empty_name_ok = pcall(function()
            set_headers:set("", "bad")
        end)
        if bad_empty_name_ok then
            error("Headers.set accepted empty name")
        end

        local bad_token_name_ok = pcall(function()
            set_headers:set("Bad Header", "bad")
        end)
        if bad_token_name_ok then
            error("Headers.set accepted invalid token name")
        end

        local bad_control_value_ok = pcall(function()
            set_headers:set("X-Bad", "bad\1value")
        end)
        if bad_control_value_ok then
            error("Headers.set accepted invalid control value")
        end

        if not set_headers:has("X-Test") then
            error("Headers.has existing header mismatch")
        end
        if not set_headers:has("x-other") then
            error("Headers.has lowercase header mismatch")
        end
        if set_headers:has("X-Missing") then
            error("Headers.has missing header mismatch")
        end

        local bad_has_ok = pcall(function()
            set_headers:has({})
        end)
        if bad_has_ok then
            error("Headers.has accepted invalid name")
        end

        local delete_headers = Headers.new({
            ["X-Test"] = "one",
            ["X-Other"] = "two",
        })
        delete_headers:delete("x-test")
        if delete_headers:has("X-Test") then
            error("Headers.delete kept deleted header")
        end
        if delete_headers:get("X-Test") ~= nil then
            error("Headers.delete get deleted header mismatch")
        end
        if not delete_headers:has("X-Other") then
            error("Headers.delete removed wrong header")
        end
        delete_headers:delete("X-Missing")
        if not delete_headers:has("X-Other") then
            error("Headers.delete missing header changed headers")
        end

        local bad_delete_ok = pcall(function()
            delete_headers:delete({})
        end)
        if bad_delete_ok then
            error("Headers.delete accepted invalid name")
        end

        local entry_count = 0
        local entries = {}
        for name, value in set_headers:entries() do
            entry_count = entry_count + 1
            entries[name] = value
        end
        if entry_count ~= 2 then
            error("Headers.entries count mismatch")
        end
        if entries["x-test"] ~= "four" then
            error("Headers.entries replaced value mismatch")
        end
        if entries["x-other"] ~= "three" then
            error("Headers.entries new value mismatch")
        end

        for _ in Headers.new():entries() do
            error("Headers.entries yielded empty headers")
        end

        local bad_entries_ok = pcall(function()
            set_headers:entries("bad")
        end)
        if bad_entries_ok then
            error("Headers.entries accepted an argument")
        end

        local request = Request.new("https://example.test/headers", {
            headers = headers,
        })
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

cat > "$TEST_ROOT/app-json.lua" <<'EOF'
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local function expect_bad(fn, message)
    local ok = pcall(fn)
    if ok then
        error(message)
    end
end

local app = App.new()

app:all("*", function(request)
    if request.url:match("/lua%-json%-response%-custom%-type$") then
        return Response.json({ ok = true }, {
            headers = { ["content-type"] = "application/problem+json" },
        })
    end

    if request.url:match("/lua%-json%-response%-errors$") then
        local ok, err = pcall(function()
            expect_bad(function()
                Response.json({ ok = true }, { status = 204 })
            end, "Response.json accepted 204 body")

            expect_bad(function()
                Response.json({ ok = true }, { body = text_stream("bad") })
            end, "Response.json accepted init body")
        end)

        if not ok then
            return Response.new({
                status = 500,
                body = text_stream(err),
            })
        end

        return Response.new({
            status = 200,
            body = text_stream("Response.json rejects invalid init"),
        })
    end

    if request.url:match("/lua%-json%-response$") then
        return Response.json({
            ok = true,
            count = 3,
            items = { 1, "two", JSON.null },
            message = "hello",
        }, {
            status = 201,
            headers = { ["X-JSON-Test"] = "ok" },
        })
    end

    local ok, err = pcall(function()
        if JSON.stringify(nil) ~= "null" then
            error("JSON.stringify nil mismatch")
        end
        if JSON.stringify(JSON.null) ~= "null" then
            error("JSON.stringify null mismatch")
        end
        if JSON.stringify(true) ~= "true" then
            error("JSON.stringify true mismatch")
        end
        if JSON.stringify(false) ~= "false" then
            error("JSON.stringify false mismatch")
        end
        if JSON.stringify({ "a", 2, false, JSON.null }) ~= '["a",2,false,null]' then
            error("JSON.stringify array mismatch")
        end
        if JSON.stringify({}) ~= "{}" then
            error("JSON.stringify empty table mismatch")
        end
        if JSON.stringify("line\nquote\"slash\\\1") ~= "\"line\\nquote\\\"slash\\\\\\u0001\"" then
            error("JSON.stringify string escape mismatch")
        end

        local parsed = JSON.parse([[
            {
                "ok": true,
                "count": 3,
                "items": [1, "two", null],
                "message": "hello\nworld",
                "unicode": "\u0041\ud83d\ude00"
            }
        ]])

        if parsed.ok ~= true then
            error("JSON.parse boolean mismatch")
        end
        if parsed.count ~= 3 then
            error("JSON.parse number mismatch")
        end
        if parsed.items[1] ~= 1 or parsed.items[2] ~= "two" then
            error("JSON.parse array mismatch")
        end
        if parsed.items[3] ~= JSON.null then
            error("JSON.parse null mismatch")
        end
        if parsed.message ~= "hello\nworld" then
            error("JSON.parse string escape mismatch")
        end
        if parsed.unicode ~= "A" .. string.char(0xf0, 0x9f, 0x98, 0x80) then
            error("JSON.parse unicode mismatch")
        end
        if JSON.stringify(parsed.items) ~= '[1,"two",null]' then
            error("JSON.stringify parsed null mismatch")
        end

        expect_bad(function()
            JSON.parse("[1,]")
        end, "JSON.parse accepted trailing comma")

        expect_bad(function()
            JSON.parse("\"\\ud800\"")
        end, "JSON.parse accepted unpaired surrogate")

        expect_bad(function()
            JSON.parse("1e9999")
        end, "JSON.parse accepted non-finite number")

        expect_bad(function()
            JSON.stringify(function() end)
        end, "JSON.stringify accepted function")

        expect_bad(function()
            JSON.stringify({ [2] = "bad" })
        end, "JSON.stringify accepted sparse array")

        expect_bad(function()
            JSON.stringify({ "bad", key = "value" })
        end, "JSON.stringify accepted mixed table")
    end)

    if not ok then
        return Response.new({
            status = 500,
            body = text_stream(err),
        })
    end

    return Response.new({
        status = 200,
        body = text_stream("JSON.stringify and JSON.parse"),
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

app:all("*", function(request)
    if request.url:match("/lua%-stream%-chunks$") then
        local chunks = ReadableStream.new({
            start = function(controller)
                controller:enqueue("first")
                controller:enqueue("second")
                controller:close()
            end,
        })
        local reader = chunks:getReader()
        local first = reader:read()
        local second = reader:read()
        local done = reader:read()

        return Response.new({
            status = 200,
            body = text_stream(first.value .. ":" .. second.value .. ":"
                               .. tostring(done.done)),
        })
    end

    if request.url:match("/lua%-stream%-error$") then
        local chunks = ReadableStream.new({
            pull = function(controller)
                controller:enqueue("before error")
                error("source failed")
            end,
        })
        local reader = chunks:getReader()
        local first_ok, first = pcall(function()
            return reader:read()
        end)
        local second_ok = pcall(function()
            reader:read()
        end)

        return Response.new({
            status = 200,
            body = text_stream(tostring(first_ok) .. ":" .. first.value
                               .. ":" .. tostring(second_ok)),
        })
    end

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

    local lock_stream = ReadableStream.new()
    local reader = lock_stream:getReader()
    local locked_ok = pcall(function()
        lock_stream:getReader()
    end)
    if locked_ok then
        return Response.new({
            status = 500,
            body = text_stream("ReadableStream allowed second reader"),
        })
    end

    reader:releaseLock()

    local released_reader_ok = pcall(function()
        reader:read()
    end)
    if released_reader_ok then
        return Response.new({
            status = 500,
            body = text_stream("released reader still read"),
        })
    end

    local second_reader = lock_stream:getReader()
    second_reader:releaseLock()

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
local header_fetch_url = "http://127.0.0.1:__HEADER_FETCH_PORT__/headers"
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
    if request.url:match("/lua%-fetch%-headers$") then
        local response, err = fetch(header_fetch_url, {
            headers = {
                ["X-Test"] = "one",
                ["Host"] = "bad.example",
                ["Connection"] = "close",
                ["Transfer-Encoding"] = "identity",
                ["Content-Length"] = "999",
            },
        })

        if response == nil or response.status ~= 200 then
            return Response.new({
                status = 500,
                body = text_stream(err or "fetch header status mismatch"),
            })
        end

        local body = read_body(response)

        if body ~= "fetch header response" then
            return Response.new({
                status = 500,
                body = text_stream(body),
            })
        end

        return Response.new({
            status = 200,
            body = text_stream("fetch sent request headers"),
        })
    end

    if request.url:match("/lua%-fetch%-request%-init$") then
        local url = upstream_url(request)
        local request_body = ReadableStream.new({
            start = function(controller)
                controller:enqueue("request body")
                controller:close()
            end,
        })
        local init_body = ReadableStream.new({
            start = function(controller)
                controller:enqueue("init ")
                controller:enqueue("body")
                controller:close()
            end,
        })
        local fetch_request = Request.new(url, {
            method = "POST",
            headers = { ["X-Test"] = "request" },
            body = request_body,
        })

        local response, err = fetch(fetch_request, {
            url = "http://127.0.0.1:1/not-used",
            method = "POST",
            body = init_body,
        })

        if response == nil or response.status ~= 200 then
            return Response.new({
                status = 500,
                body = text_stream(err or "fetch Request init status mismatch"),
            })
        end

        local body = read_body(response)

        if body ~= "fetch init response" then
            return Response.new({
                status = 500,
                body = text_stream(body),
            })
        end

        local empty_init_request = Request.new(url, {
            method = "POST",
            body = text_stream("init body"),
        })

        response, err = fetch(empty_init_request, {})

        if response == nil or response.status ~= 200 then
            return Response.new({
                status = 500,
                body = text_stream(err or "fetch Request empty init status mismatch"),
            })
        end

        body = read_body(response)

        if body ~= "fetch init response" then
            return Response.new({
                status = 500,
                body = text_stream(body),
            })
        end

        local header_request = Request.new(header_fetch_url, {
            headers = {
                ["X-Test"] = "request",
                ["X-Old"] = "old",
            },
        })

        response, err = fetch(header_request, {
            headers = { ["X-Test"] = "one" },
        })

        if response == nil or response.status ~= 200 then
            return Response.new({
                status = 500,
                body = text_stream(err or "fetch Request headers status mismatch"),
            })
        end

        body = read_body(response)

        if body ~= "fetch header response" then
            return Response.new({
                status = 500,
                body = text_stream(body),
            })
        end

        if header_request.headers:get("X-Test") ~= "request"
            or header_request.headers:get("X-Old") ~= "old"
        then
            return Response.new({
                status = 500,
                body = text_stream("fetch mutated input Request"),
            })
        end

        return Response.new({
            status = 200,
            body = text_stream("fetch Request init merged"),
        })
    end

    if request.url:match("/lua%-fetch%-body%-mixin$") then
        local url = upstream_url(request)
        local response, err = fetch(url, {
            method = "POST",
            body = text_stream("json body"),
        })

        if response == nil or response.status ~= 200 then
            return Response.new({
                status = 500,
                body = text_stream(err or "fetch body json status mismatch"),
            })
        end

        local data = response:json()
        if data.ok ~= true
            or data.count ~= 4
            or data.message ~= "fetch json response"
        then
            return Response.new({
                status = 500,
                body = text_stream("fetch Response:json mismatch"),
            })
        end

        if not response.bodyUsed then
            return Response.new({
                status = 500,
                body = text_stream("fetch Response:json bodyUsed missing"),
            })
        end

        response, err = fetch(url, {
            method = "POST",
            body = text_stream("text body"),
        })

        if response == nil or response.status ~= 200 then
            return Response.new({
                status = 500,
                body = text_stream(err or "fetch body text status mismatch"),
            })
        end

        local text = response:text()
        if text ~= "fetch text response" then
            return Response.new({
                status = 500,
                body = text_stream("fetch Response:text mismatch"),
            })
        end

        local second_ok = pcall(function()
            response:json()
        end)
        if second_ok then
            return Response.new({
                status = 500,
                body = text_stream("fetch Response methods reused body"),
            })
        end

        return Response.new({
            status = 200,
            body = text_stream(data.message .. ":" .. text),
        })
    end

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
        }, { tls_verify = false })

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

    if request.url:match("/lua%-fetch%-https%-verify%-fail$") then
        local response, err = fetch(https_fetch_url)

        if response ~= nil then
            return Response.new({
                status = 500,
                body = text_stream("fetch HTTPS verify returned response"),
            })
        end

        if err ~= "fetch SSL certificate verify failed" then
            return Response.new({
                status = 500,
                body = text_stream(err or "fetch HTTPS verify error missing"),
            })
        end

        return Response.new({
            status = 200,
            body = text_stream("fetch HTTPS verify failed"),
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

    local bad_table_fetch_ok = pcall(function()
        fetch({ url = url })
    end)
    if bad_table_fetch_ok then
        return Response.new({
            status = 500,
            body = text_stream("fetch accepted table input"),
        })
    end

    local bad_table_fetch_init_ok = pcall(function()
        fetch({ url = url }, {})
    end)
    if bad_table_fetch_init_ok then
        return Response.new({
            status = 500,
            body = text_stream("fetch accepted table input with init"),
        })
    end

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
sed -i "s/__HEADER_FETCH_PORT__/$HEADER_FETCH_PORT/g" "$TEST_ROOT/app-fetch.lua"

cat > "$TEST_ROOT/app-fetch-upload.lua" <<'EOF'
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local app = App.new()
local target = "http://127.0.0.1:__FETCH_UPLOAD_PORT__"

app:all("*", function(request)
    local response, err = fetch(request, nil, { target = target })

    if response == nil then
        return Response.new({
            status = 500,
            body = text_stream(err or "fetch upload failed"),
        })
    end

    return Response.new({
        status = response.status,
        body = response.body,
    })
end)

return app
EOF

sed -i "s/__FETCH_UPLOAD_PORT__/$FETCH_UPLOAD_PORT/g" \
    "$TEST_ROOT/app-fetch-upload.lua"

cat > "$TEST_ROOT/app-fetch-timeout.lua" <<'EOF'
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local app = App.new()
local slow_url = "http://127.0.0.1:__SLOW_FETCH_PORT__/slow"

app:all("*", function()
    local response, err = fetch(slow_url, nil, {
        connect_timeout = 1000,
        send_timeout = 1000,
        read_timeout = 100,
        keepalive_timeout = 1000,
    })

    if response ~= nil then
        return Response.new({
            status = 500,
            body = text_stream("fetch timeout returned response"),
        })
    end

    return Response.new({
        status = 200,
        body = text_stream(err or "fetch timeout error missing"),
    })
end)

return app
EOF

sed -i "s/__SLOW_FETCH_PORT__/$SLOW_FETCH_PORT/g" "$TEST_ROOT/app-fetch-timeout.lua"

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

    if body == "json body" then
        return Response.json({
            ok = true,
            count = 4,
            message = "fetch json response",
        }, {
            headers = { ["X-Fetch-Upstream"] = "ok" },
        })
    end

    if body == "text body" then
        return Response.new({
            status = 200,
            headers = { ["X-Fetch-Upstream"] = "ok" },
            body = text_stream("fetch text response"),
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

cat > "$TEST_ROOT/app-vm-write.lua" <<'EOF'
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
    _G.__lua_web_main_conf_vm_marker = "shared main conf VM"

    return Response.new({
        status = 200,
        body = text_stream("lua VM writer set"),
    })
end)

return app
EOF

cat > "$TEST_ROOT/app-vm-read.lua" <<'EOF'
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
    if _G.__lua_web_main_conf_vm_marker ~= "shared main conf VM" then
        return Response.new({
            status = 500,
            body = text_stream("lua VM marker missing"),
        })
    end

    return Response.new({
        status = 200,
        body = text_stream("lua VM shared"),
    })
end)

return app
EOF

cat > "$TEST_ROOT/app-no-content.lua" <<'EOF'
local app = App.new()

app:all("*", function()
    return Response.new({ status = 204 })
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

local invalid_patterns = {
    "/lua-methods/a:b",
    "/lua-methods/:",
    "/lua-methods/:1",
    "/lua-methods/:id-name",
    "/lua-methods/:id/:id",
    "lua-methods/:id",
    "lua-methods",
}

local invalid_param_errors = {}

for _, pattern in ipairs(invalid_patterns) do
    local ok = pcall(function()
        app:get(pattern, function()
            return Response.new({
                status = 500,
                body = text_stream("invalid pattern matched"),
            })
        end)
    end)

    if ok then
        invalid_param_errors[#invalid_param_errors + 1] = pattern
    end
end

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

app:get("/lua-methods/invalid-param-pattern", function()
    if #invalid_param_errors ~= 0 then
        return Response.new({
            status = 500,
            body = text_stream(
                "invalid route parameter pattern accepted: "
                .. table.concat(invalid_param_errors, ",")),
        })
    end

    return Response.new({
        status = 200,
        body = text_stream("invalid route parameter pattern rejected"),
    })
end)

app:get("/lua-methods/params-empty", function(_, params)
    if type(params) ~= "table" then
        return Response.new({
            status = 500,
            body = text_stream("params missing"),
        })
    end

    if next(params) ~= nil then
        return Response.new({
            status = 500,
            body = text_stream("params should be empty"),
        })
    end

    return Response.new({
        status = 200,
        body = text_stream("params empty"),
    })
end)

app:get("/lua-methods/users/:id/posts/:post_id", function(_, params)
    if type(params) ~= "table" then
        return Response.new({
            status = 500,
            body = text_stream("params missing"),
        })
    end

    if params.id == nil or params.post_id == nil then
        return Response.new({
            status = 500,
            body = text_stream("params values missing"),
        })
    end

    return Response.new({
        status = 200,
        body = text_stream(params.id .. ":" .. params.post_id),
    })
end)

app:get("/lua-methods/prefix/*", function()
    return Response.new({
        status = 200,
        body = text_stream("prefix first"),
    })
end)

app:get("/lua-methods/prefix/exact", function()
    return Response.new({
        status = 200,
        body = text_stream("exact after prefix"),
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

cat > "$TEST_ROOT/app-non-string-error.lua" <<'EOF'
error({ source = "file" })
EOF

cat > "$TEST_ROOT/app-non-string-handler-error.lua" <<'EOF'
local app = App.new()

app:all("*", function()
    error({ source = "handler" })
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

        location /lua-request-headers {
            lua_web_file $TEST_ROOT/app-request-headers.lua;
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
            client_max_body_size 32m;
            lua_web_file $TEST_ROOT/app-body-stream.lua;
        }

        location /lua-response-new {
            lua_web_file $TEST_ROOT/app-response-new.lua;
        }

        location /lua-headers-new {
            lua_web_file $TEST_ROOT/app-headers-new.lua;
        }

        location /lua-json {
            lua_web_file $TEST_ROOT/app-json.lua;
        }

        location /lua-url {
            lua_web_file $TEST_ROOT/app-url.lua;
        }

        location /lua-stream {
            lua_web_file $TEST_ROOT/app-stream.lua;
        }

        location /lua-stream-chunks {
            lua_web_file $TEST_ROOT/app-stream.lua;
        }

        location /lua-stream-error {
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

        location /lua-fetch-https-verify-fail {
            lua_web_file $TEST_ROOT/app-fetch.lua;
        }

        location /lua-fetch-headers {
            lua_web_file $TEST_ROOT/app-fetch.lua;
        }

        location /lua-fetch-request-init {
            lua_web_file $TEST_ROOT/app-fetch.lua;
        }

        location /lua-fetch-timeout {
            lua_web_file $TEST_ROOT/app-fetch-timeout.lua;
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

        location /lua-fetch-upload {
            client_max_body_size 64m;
            lua_web_file $TEST_ROOT/app-fetch-upload.lua;
        }

        location /fetch-upstream {
            lua_web_file $TEST_ROOT/app-fetch-upstream.lua;
        }

        location /lua-alt {
            lua_web_file $TEST_ROOT/app-alt.lua;
        }

        location /lua-vm-write {
            lua_web_file $TEST_ROOT/app-vm-write.lua;
        }

        location /lua-vm-read {
            lua_web_file $TEST_ROOT/app-vm-read.lua;
        }

        location /lua-non-string-file-error {
            lua_web_file $TEST_ROOT/app-non-string-error.lua;
        }

        location /lua-non-string-handler-error {
            lua_web_file $TEST_ROOT/app-non-string-handler-error.lua;
        }

        location /lua-subrequest-mirror {
            mirror /lua-subrequest-target;
            mirror_request_body off;
            lua_web_file $TEST_ROOT/app-no-content.lua;
        }

        location /lua-subrequest-target {
            lua_web_file $TEST_ROOT/app.lua;
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

SLOW_FETCH_PID=

python3 - "$SLOW_FETCH_PORT" <<'PY' &
import socket
import sys
import time

port = int(sys.argv[1])
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(("127.0.0.1", port))
server.listen(16)

while True:
    conn, _ = server.accept()

    with conn:
        conn.settimeout(1)
        data = b""

        try:
            while b"\r\n\r\n" not in data:
                chunk = conn.recv(4096)
                if not chunk:
                    break

                data += chunk

        except OSError:
            pass

        time.sleep(0.3)

        try:
            conn.sendall(
                b"HTTP/1.1 200 OK\r\n"
                b"Content-Length: 4\r\n"
                b"Connection: close\r\n"
                b"\r\n"
                b"slow"
            )

        except OSError:
            pass
PY
SLOW_FETCH_PID=$!

HEADER_FETCH_PID=

python3 - "$HEADER_FETCH_PORT" <<'PY' &
import socket
import sys

port = int(sys.argv[1])
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(("127.0.0.1", port))
server.listen(16)

while True:
    conn, _ = server.accept()

    with conn:
        conn.settimeout(1)
        data = b""

        try:
            while b"\r\n\r\n" not in data:
                chunk = conn.recv(4096)
                if not chunk:
                    break

                data += chunk

        except OSError:
            pass

        lines = data.split(b"\r\n\r\n", 1)[0].split(b"\r\n")
        headers = {}

        for line in lines[1:]:
            name, sep, value = line.partition(b":")
            if sep != b":":
                continue

            headers.setdefault(name.strip().lower(), []).append(value.strip())

        body = b"fetch header response"

        if headers.get(b"x-test") != [b"one"]:
            body = b"fetch request header missing"

        elif any(value == b"bad.example" for value in headers.get(b"host", [])):
            body = b"fetch forwarded controlled Host header"

        elif headers.get(b"connection") != [b"keep-alive"]:
            body = b"fetch forwarded controlled Connection header"

        elif b"content-length" in headers:
            body = b"fetch forwarded controlled Content-Length header"

        elif b"transfer-encoding" in headers:
            body = b"fetch forwarded controlled Transfer-Encoding header"

        elif b"x-old" in headers:
            body = b"fetch merged request header"

        try:
            conn.sendall(
                b"HTTP/1.1 200 OK\r\n"
                b"Content-Length: " + str(len(body)).encode() + b"\r\n"
                b"Connection: close\r\n"
                b"\r\n" + body
            )

        except OSError:
            pass
PY
HEADER_FETCH_PID=$!

FETCH_UPLOAD_PID=

python3 "$MODULE_DIR/tests/fetch_upload_sink.py" \
    "$FETCH_UPLOAD_PORT" "$TEST_ROOT" &
FETCH_UPLOAD_PID=$!

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
    if [ -n "$SLOW_FETCH_PID" ]; then
        kill "$SLOW_FETCH_PID" >/dev/null 2>&1 || true
    fi
    if [ -n "$HEADER_FETCH_PID" ]; then
        kill "$HEADER_FETCH_PID" >/dev/null 2>&1 || true
    fi
    if [ -n "$FETCH_UPLOAD_PID" ]; then
        kill "$FETCH_UPLOAD_PID" >/dev/null 2>&1 || true
    fi
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
    test_module.py \
    test_app.py \
    test_requests.py \
    test_response.py \
    test_headers.py \
    test_url.py \
    test_stream.py \
    test_fetch.py \
    test_json.py
do
    TEST_NGINX_PORT="$PORT" \
    TEST_NGINX_ROOT="$TEST_ROOT" \
    TEST_NGINX_HAVE_HTTP_SSL="$HAVE_HTTP_SSL" \
        python3 "$MODULE_DIR/tests/$test_file"
done
