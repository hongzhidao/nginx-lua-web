#!/bin/sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
PORT=${TEST_NGINX_PORT:-18080}
PREFIX=$(mktemp -d "${TMPDIR:-/tmp}/nginx-lua-test.XXXXXX")
NGINX=${NGINX:-$ROOT/../nginx/objs/nginx}

abs_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s\n' "$ROOT/$1" ;;
    esac
}

find_nginx() {
    NGINX=$(abs_path "$NGINX")

    [ -x "$NGINX" ] || {
        printf 'Nginx binary is not executable: %s\n' "$NGINX" >&2
        exit 1
    }
}

cleanup() {
    if [ -n "${nginx_pid:-}" ]; then
        kill "$nginx_pid" >/dev/null 2>&1 || true
        wait "$nginx_pid" 2>/dev/null || true
    fi

    rm -rf "$PREFIX"
}

find_nginx

mkdir -p "$PREFIX/conf" "$PREFIX/logs" "$PREFIX/app"

TEST_MARKER="$PREFIX/logs/lua-handler-ran"
export TEST_MARKER

sed "s/127.0.0.1:18080/127.0.0.1:$PORT/" \
    "$ROOT/tests/fixtures/nginx.conf" > "$PREFIX/conf/nginx.conf"
cp "$ROOT/tests/fixtures/app/hello.lua" "$PREFIX/app/hello.lua"

"$NGINX" -p "$PREFIX/" -c conf/nginx.conf &
nginx_pid=$!
trap cleanup EXIT

if ! TEST_URL="http://127.0.0.1:$PORT/hello" \
    python3 "$ROOT/tests/test_lua.py"
then
    [ -f "$PREFIX/logs/error.log" ] &&
        sed -n '1,160p' "$PREFIX/logs/error.log" >&2
    exit 1
fi
