import http.client
import os
import socket
import sys
import time


HOST = os.environ.get("TEST_NGINX_HOST", "127.0.0.1")
PORT = int(os.environ["TEST_NGINX_PORT"])
LUA_WEB_PATH = os.environ.get("TEST_LUA_WEB_PATH", "/lua")


def request(path, method="GET", body=None, headers=None):
    deadline = time.time() + 5
    last_error = None

    while time.time() < deadline:
        conn = None
        try:
            conn = http.client.HTTPConnection(HOST, PORT, timeout=1)
            conn.request(method, path, body=body, headers=headers or {})
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


def delayed_body_request(path, payload):
    body = payload.encode()
    split = len(body) // 2
    request_head = (
        f"POST {path} HTTP/1.1\r\n"
        f"Host: {HOST}:{PORT}\r\n"
        f"Content-Length: {len(body)}\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).encode()

    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(2)
        sock.sendall(request_head)
        time.sleep(0.1)
        sock.sendall(body[:split])
        time.sleep(0.1)
        sock.sendall(body[split:])

        data = b""
        while b"\r\n\r\n" not in data:
            chunk = sock.recv(4096)
            if not chunk:
                break
            data += chunk

        head, _, response_body = data.partition(b"\r\n\r\n")
        status_line = head.splitlines()[0].decode()
        status = int(status_line.split()[1])
        content_length = None

        for line in head.splitlines()[1:]:
            name, _, value = line.partition(b":")
            if name.lower() == b"content-length":
                content_length = int(value.strip())
                break

        if content_length is not None:
            while len(response_body) < content_length:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response_body += chunk
        else:
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response_body += chunk

    return status, response_body.decode()


def test_lua_web_file_returns_status_and_text():
    status, body = request(LUA_WEB_PATH)

    if status != 201:
        raise AssertionError(f"expected 201, got {status}")

    if body != "hello from lua handler":
        raise AssertionError(f"expected lua response body, got {body!r}")


def test_lua_web_file_keeps_location_refs_separate():
    status, body = request("/lua-alt")

    if status != 202:
        raise AssertionError(f"expected 202, got {status}")

    if body != "hello from second lua handler":
        raise AssertionError(f"expected second lua response body, got {body!r}")


def test_app_new_rejects_arguments():
    status, body = request("/lua-app-args")

    if status != 203:
        raise AssertionError(f"expected 203, got {status}")

    if body != "App.new rejected arguments":
        raise AssertionError(f"expected App.new rejection body, got {body!r}")


def test_coroutine_library_is_not_exposed():
    status, body = request("/lua-coroutine-disabled")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "coroutine disabled":
        raise AssertionError(f"expected coroutine disabled body, got {body!r}")


def test_request_body_readable_stream_reader_reads_body():
    payload = "hello from request body"
    status, body = request("/lua-body", method="POST", body=payload)

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != payload:
        raise AssertionError(f"expected echoed request body, got {body!r}")


def test_request_body_reader_yields_until_body_arrives():
    payload = "delayed request body"
    status, body = delayed_body_request("/lua-body", payload)

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != payload:
        raise AssertionError(f"expected delayed request body, got {body!r}")


def main():
    tests = [
        ("lua handler returns status and text",
         test_lua_web_file_returns_status_and_text),
        ("location refs stay separate",
         test_lua_web_file_keeps_location_refs_separate),
        ("App.new rejects arguments",
         test_app_new_rejects_arguments),
        ("coroutine library is not exposed",
         test_coroutine_library_is_not_exposed),
        ("request body reader reads body",
         test_request_body_readable_stream_reader_reads_body),
        ("request body reader yields until body arrives",
         test_request_body_reader_yields_until_body_arrives),
    ]

    try:
        for name, test in tests:
            test()
            print(f"ok - {name}")

    except Exception as exc:
        print(f"not ok - lua_web_file behavior: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
