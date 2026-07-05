#!/bin/sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
NGINX=${NGINX:-"$ROOT/../nginx/objs/nginx"}
WRK=${WRK:-wrk}
BUN=${BUN:-bun}
THREADS=${THREADS:-2}
CONNECTIONS=${CONNECTIONS:-100}
DURATION=${DURATION:-10s}
WARMUP=${WARMUP:-3s}
NGINX_UPSTREAM_KEEPALIVE=${NGINX_UPSTREAM_KEEPALIVE:-$CONNECTIONS}
LUA_PROXY_DIRECT_RESPONSE=${LUA_PROXY_DIRECT_RESPONSE:-false}
TEST_ROOT=${TEST_ROOT:-$(mktemp -d /tmp/nginx-lua-web-proxy-perf.XXXXXX)}

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

if ! command -v "$BUN" >/dev/null 2>&1; then
    echo "bun not found" >&2
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
NGINX_PROXY_PORT=${NGINX_PROXY_PORT:-$(pick_port "$UPSTREAM_PORT")}
LUA_PROXY_PORT=${LUA_PROXY_PORT:-$(pick_port "$UPSTREAM_PORT" "$NGINX_PROXY_PORT")}
BUN_PROXY_PORT=${BUN_PROXY_PORT:-$(pick_port "$UPSTREAM_PORT" "$NGINX_PROXY_PORT" "$LUA_PROXY_PORT")}

mkdir -p "$TEST_ROOT/upstream/conf" "$TEST_ROOT/upstream/logs"
mkdir -p "$TEST_ROOT/proxy/conf" "$TEST_ROOT/proxy/logs"

cleanup() {
    if [ -f "$TEST_ROOT/proxy/logs/nginx.pid" ]; then
        "$NGINX" -p "$TEST_ROOT/proxy/" -c conf/nginx.conf -s quit >/dev/null 2>&1 || true
    fi

    if [ -f "$TEST_ROOT/upstream/logs/nginx.pid" ]; then
        "$NGINX" -p "$TEST_ROOT/upstream/" -c conf/nginx.conf -s quit >/dev/null 2>&1 || true
    fi

    if [ -n "${BUN_PID:-}" ]; then
        kill "$BUN_PID" >/dev/null 2>&1 || true
        wait "$BUN_PID" 2>/dev/null || true
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

    upstream perf_upstream {
        server 127.0.0.1:$UPSTREAM_PORT;
        keepalive $NGINX_UPSTREAM_KEEPALIVE;
    }

    server {
        listen 127.0.0.1:$NGINX_PROXY_PORT;

        location / {
            proxy_http_version 1.1;
            proxy_set_header Connection "";
            proxy_set_header Host 127.0.0.1:$UPSTREAM_PORT;
            proxy_pass http://perf_upstream;
        }
    }

    server {
        listen 127.0.0.1:$LUA_PROXY_PORT;

        location / {
            lua_web_file $TEST_ROOT/proxy/app.lua;
        }
    }
}
EOF

cat > "$TEST_ROOT/bun-proxy.js" <<EOF
const upstream = "http://127.0.0.1:$UPSTREAM_PORT";

Bun.serve({
  hostname: "127.0.0.1",
  port: $BUN_PROXY_PORT,
  reusePort: false,
  async fetch(request) {
    const url = new URL(request.url);
    const init = {
      method: request.method,
      headers: request.headers,
      redirect: "manual",
    };

    if (request.method !== "GET" && request.method !== "HEAD") {
      init.body = request.body;
      init.duplex = "half";
    }

    return fetch(upstream + url.pathname + url.search, init);
  },
});
EOF

"$NGINX" -p "$TEST_ROOT/upstream/" -c conf/nginx.conf
"$NGINX" -p "$TEST_ROOT/proxy/" -c conf/nginx.conf
"$BUN" "$TEST_ROOT/bun-proxy.js" >"$TEST_ROOT/bun.log" 2>&1 &
BUN_PID=$!

sleep 1

check() {
    name=$1
    url=$2
    status=$(curl -sS -o /dev/null -w '%{http_code}' "$url")
    if [ "$status" != 200 ]; then
        echo "$name failed health check: HTTP $status" >&2
        exit 1
    fi
}

UPSTREAM_URL="http://127.0.0.1:$UPSTREAM_PORT/plain"
NGINX_PROXY_URL="http://127.0.0.1:$NGINX_PROXY_PORT/plain"
LUA_PROXY_URL="http://127.0.0.1:$LUA_PROXY_PORT/plain"
BUN_PROXY_URL="http://127.0.0.1:$BUN_PROXY_PORT/plain"

check upstream "$UPSTREAM_URL"
check nginx_proxy "$NGINX_PROXY_URL"
check lua_proxy "$LUA_PROXY_URL"
check bun_proxy "$BUN_PROXY_URL"

echo "nginx: $("$NGINX" -v 2>&1)"
echo "bun: $("$BUN" --version)"
echo "wrk: $("$WRK" -v 2>&1 | head -n 1)"
echo "threads=$THREADS connections=$CONNECTIONS duration=$DURATION warmup=$WARMUP nginx_upstream_keepalive=$NGINX_UPSTREAM_KEEPALIVE lua_proxy_direct_response=$LUA_PROXY_DIRECT_RESPONSE"
echo "test_root=$TEST_ROOT"
echo

run_case() {
    name=$1
    url=$2

    "$WRK" -t"$THREADS" -c"$CONNECTIONS" -d"$WARMUP" "$url" >/dev/null

    echo "## $name"
    echo "$url"
    "$WRK" -t"$THREADS" -c"$CONNECTIONS" -d"$DURATION" --latency "$url"
    echo
}

run_case direct_upstream "$UPSTREAM_URL"
run_case nginx_proxy "$NGINX_PROXY_URL"
run_case lua_web_fetch_proxy "$LUA_PROXY_URL"
run_case bun_fetch_proxy "$BUN_PROXY_URL"
