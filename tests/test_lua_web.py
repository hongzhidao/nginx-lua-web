import http.client
import os
import sys
import time


HOST = os.environ.get("TEST_NGINX_HOST", "127.0.0.1")
PORT = int(os.environ["TEST_NGINX_PORT"])
LUA_WEB_PATH = os.environ.get("TEST_LUA_WEB_PATH", "/lua")


def request(path):
    deadline = time.time() + 5
    last_error = None

    while time.time() < deadline:
        conn = None
        try:
            conn = http.client.HTTPConnection(HOST, PORT, timeout=1)
            conn.request("GET", path)
            response = conn.getresponse()
            body = response.read().decode()
            return response.status, body

        except OSError as exc:
            last_error = exc
            time.sleep(0.1)

        finally:
            if conn is not None:
                conn.close()

    raise AssertionError(f"nginx did not accept connections: {last_error}")


def test_lua_web_file_returns_status_and_text():
    status, body = request(LUA_WEB_PATH)

    if status != 201:
        raise AssertionError(f"expected 201, got {status}")

    if body != "hello from lua":
        raise AssertionError(f"expected lua response body, got {body!r}")


def test_lua_web_file_keeps_location_refs_separate():
    status, body = request("/lua-alt")

    if status != 202:
        raise AssertionError(f"expected 202, got {status}")

    if body != "hello from second lua":
        raise AssertionError(f"expected second lua response body, got {body!r}")


def main():
    try:
        test_lua_web_file_returns_status_and_text()
        test_lua_web_file_keeps_location_refs_separate()
    except Exception as exc:
        print(f"not ok - lua_web_file returns status and text: {exc}",
              file=sys.stderr)
        return 1

    print("ok - lua_web_file returns status and text")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
