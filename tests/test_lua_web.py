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
        chunked = False

        for line in head.splitlines()[1:]:
            name, _, value = line.partition(b":")
            if name.lower() == b"content-length":
                content_length = int(value.strip())
            elif name.lower() == b"transfer-encoding":
                chunked = b"chunked" in value.lower()

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

    if chunked:
        response_body = decode_chunked(response_body)

    return status, response_body.decode()


def decode_chunked(data):
    pos = 0
    body = []

    while True:
        line_end = data.find(b"\r\n", pos)
        if line_end == -1:
            raise AssertionError("incomplete chunked response")

        size_line = data[pos:line_end].split(b";", 1)[0]
        size = int(size_line, 16)
        pos = line_end + 2

        if size == 0:
            break

        body.append(data[pos:pos + size])
        pos += size

        if data[pos:pos + 2] != b"\r\n":
            raise AssertionError("invalid chunked response")
        pos += 2

    return b"".join(body)


def test_lua_web_file_returns_status_and_stream():
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


def test_response_stream_waits_for_request_body():
    payload = "streamed request body"
    status, body = delayed_body_request("/lua-body-stream", payload)

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != payload:
        raise AssertionError(f"expected streamed request body, got {body!r}")


def test_readable_stream_new_and_controller_enqueue():
    status, body = request("/lua-stream")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "hello from lua stream":
        raise AssertionError(f"expected lua stream body, got {body!r}")


def test_readable_stream_pull_source():
    status, body = request("/lua-stream-pull")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "pulled from source":
        raise AssertionError(f"expected pull stream body, got {body!r}")


def test_request_and_headers_new():
    status, body = request("/lua-request-headers-new")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "Request.new and Headers.new":
        raise AssertionError(f"expected constructor body, got {body!r}")


def main():
    tests = [
        ("lua handler returns status and stream",
         test_lua_web_file_returns_status_and_stream),
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
        ("response stream waits for request body",
         test_response_stream_waits_for_request_body),
        ("ReadableStream.new and controller enqueue",
         test_readable_stream_new_and_controller_enqueue),
        ("ReadableStream pull source",
         test_readable_stream_pull_source),
        ("Request.new and Headers.new",
         test_request_and_headers_new),
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
