#!/bin/sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
NGINX=${NGINX:-"$ROOT/../nginx/objs/nginx"}
WRK=${WRK:-wrk}
PERF=${PERF:-perf}
THREADS=${THREADS:-2}
CONNECTIONS=${CONNECTIONS:-100}
DURATION=${DURATION:-20s}
WARMUP=${WARMUP:-5s}
PERF_FREQ=${PERF_FREQ:-997}
LUA_PROXY_DIRECT_RESPONSE=${LUA_PROXY_DIRECT_RESPONSE:-false}
TEST_ROOT=${TEST_ROOT:-$(mktemp -d /tmp/nginx-lua-web-proxy-profile.XXXXXX)}

case "$LUA_PROXY_DIRECT_RESPONSE" in
    true|false) ;;
    *)
        echo "LUA_PROXY_DIRECT_RESPONSE must be true or false" >&2
        exit 1
        ;;
esac

if [ ! -x "$NGINX" ]; then
    echo "nginx binary not found: $NGINX" >&2
    exit 1
fi

if ! command -v "$WRK" >/dev/null 2>&1; then
    echo "wrk not found" >&2
    exit 1
fi

if ! command -v "$PERF" >/dev/null 2>&1; then
    echo "perf not found" >&2
    exit 1
fi

pick_port() {
    python3 - "$@" <<'PY'
import socket
import sys

used = {int(arg) for arg in sys.argv[1:]}

while True:
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    if port not in used:
        print(port)
        break
PY
}

UPSTREAM_PORT=${UPSTREAM_PORT:-$(pick_port)}
LUA_PROXY_PORT=${LUA_PROXY_PORT:-$(pick_port "$UPSTREAM_PORT")}

mkdir -p "$TEST_ROOT/upstream/conf" "$TEST_ROOT/upstream/logs"
mkdir -p "$TEST_ROOT/proxy/conf" "$TEST_ROOT/proxy/logs"

cleanup() {
    if [ -f "$TEST_ROOT/proxy/logs/nginx.pid" ]; then
        "$NGINX" -p "$TEST_ROOT/proxy/" -c conf/nginx.conf -s quit >/dev/null 2>&1 || true
    fi

    if [ -f "$TEST_ROOT/upstream/logs/nginx.pid" ]; then
        "$NGINX" -p "$TEST_ROOT/upstream/" -c conf/nginx.conf -s quit >/dev/null 2>&1 || true
    fi
}

trap cleanup EXIT INT TERM

cat > "$TEST_ROOT/upstream/conf/nginx.conf" <<EOF
worker_processes 1;
error_log logs/error.log error;
pid logs/nginx.pid;

events {
    worker_connections 4096;
}

http {
    access_log off;
    keepalive_timeout 65s;

    server {
        listen 127.0.0.1:$UPSTREAM_PORT;

        location /plain {
            return 200 "hello from upstream\n";
        }
    }
}
EOF

cat > "$TEST_ROOT/proxy/app.lua" <<EOF
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local app = App.new()
local direct_response = $LUA_PROXY_DIRECT_RESPONSE

app:all("*", function(request)
    local response, err = fetch(request, nil, {
        target = "http://127.0.0.1:$UPSTREAM_PORT",
        connect_timeout = 1000,
        send_timeout = 1000,
        read_timeout = 5000,
        keepalive_timeout = 60000,
    })

    if response == nil then
        return Response.new({
            status = 502,
            headers = { ["content-type"] = "text/plain" },
            body = text_stream("fetch failed: " .. err .. "\n"),
        })
    end

    if direct_response then
        return response
    end

    return Response.new({
        status = response.status,
        headers = response.headers,
        body = response.body,
    })
end)

return app
EOF

cat > "$TEST_ROOT/proxy/conf/nginx.conf" <<EOF
worker_processes 1;
error_log logs/error.log error;
pid logs/nginx.pid;

events {
    worker_connections 8192;
}

http {
    access_log off;
    keepalive_timeout 65s;

    server {
        listen 127.0.0.1:$LUA_PROXY_PORT;

        location / {
            lua_web_file $TEST_ROOT/proxy/app.lua;
        }
    }
}
EOF

"$NGINX" -p "$TEST_ROOT/upstream/" -c conf/nginx.conf
"$NGINX" -p "$TEST_ROOT/proxy/" -c conf/nginx.conf

sleep 1

LUA_PROXY_URL="http://127.0.0.1:$LUA_PROXY_PORT/plain"
status=$(curl -sS -o /dev/null -w '%{http_code}' "$LUA_PROXY_URL")
if [ "$status" != 200 ]; then
    echo "lua proxy health check failed: HTTP $status" >&2
    exit 1
fi

PROXY_MASTER=$(cat "$TEST_ROOT/proxy/logs/nginx.pid")
PROXY_WORKER=$(pgrep -P "$PROXY_MASTER" nginx | head -n 1)
PERF_DATA="$TEST_ROOT/perf.data"

echo "nginx: $("$NGINX" -v 2>&1)"
echo "threads=$THREADS connections=$CONNECTIONS duration=$DURATION warmup=$WARMUP lua_proxy_direct_response=$LUA_PROXY_DIRECT_RESPONSE"
echo "lua_proxy_url=$LUA_PROXY_URL"
echo "proxy_master=$PROXY_MASTER proxy_worker=$PROXY_WORKER"
echo "test_root=$TEST_ROOT"
echo

"$WRK" -t"$THREADS" -c"$CONNECTIONS" -d"$WARMUP" "$LUA_PROXY_URL" >/dev/null

"$PERF" record -e cpu-clock -F "$PERF_FREQ" -g \
    -p "$PROXY_WORKER" -o "$PERF_DATA" -- \
    "$WRK" -t"$THREADS" -c"$CONNECTIONS" -d"$DURATION" --latency "$LUA_PROXY_URL"

echo
echo "## perf report --no-children"
"$PERF" report -i "$PERF_DATA" --stdio --no-children \
    --sort comm,dso,symbol | sed -n '1,80p'

echo
echo "## perf report --children"
"$PERF" report -i "$PERF_DATA" --stdio --children \
    --sort comm,dso,symbol | sed -n '1,80p'
