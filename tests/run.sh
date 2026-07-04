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
local app = App.new()
app:all("*", function()
    return { 201, "hello from lua handler" }
end)
return app
EOF

cat > "$TEST_ROOT/app-alt.lua" <<'EOF'
local app = App.new()
app:all("/lua-alt", function()
    return { 202, "hello from second lua handler" }
end)
return app
EOF

cat > "$TEST_ROOT/app-args.lua" <<'EOF'
local ok = pcall(function()
    App.new("bad")
end)

local app = App.new()

app:all("*", function()
    if ok then
        return { 500, "App.new accepted an argument" }
    end

    return { 203, "App.new rejected arguments" }
end)

return app
EOF

cat > "$TEST_ROOT/app-coroutine-disabled.lua" <<'EOF'
local app = App.new()
local require_ok = pcall(require, "coroutine")

app:all("*", function()
    if coroutine ~= nil then
        return { 500, "coroutine global is available" }
    end

    if require_ok then
        return { 500, "coroutine module is available" }
    end

    return { 200, "coroutine disabled" }
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
