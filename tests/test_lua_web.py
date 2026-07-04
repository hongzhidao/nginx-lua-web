import http.client
import os
import sys
import time


HOST = os.environ.get("TEST_NGINX_HOST", "127.0.0.1")
PORT = int(os.environ["TEST_NGINX_PORT"])
LUA_WEB_PATH = os.environ.get("TEST_LUA_WEB_PATH", "/lua")


def request_status(path):
    deadline = time.time() + 5
    last_error = None

    while time.time() < deadline:
        conn = None
        try:
            conn = http.client.HTTPConnection(HOST, PORT, timeout=1)
            conn.request("GET", path)
            response = conn.getresponse()
            response.read()
            return response.status

        except OSError as exc:
            last_error = exc
            time.sleep(0.1)

        finally:
            if conn is not None:
                conn.close()

    raise AssertionError(f"nginx did not accept connections: {last_error}")


def test_lua_web_file_returns_404():
    status = request_status(LUA_WEB_PATH)

    if status != 404:
        raise AssertionError(f"expected 404, got {status}")


def main():
    try:
        test_lua_web_file_returns_404()
    except Exception as exc:
        print(f"not ok - lua_web_file returns 404: {exc}", file=sys.stderr)
        return 1

    print("ok - lua_web_file returns 404")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
