import http.client
import os
import socket
import sys
import time


HOST = os.environ.get("TEST_NGINX_HOST", "127.0.0.1")
PORT = int(os.environ["TEST_NGINX_PORT"])
LUA_WEB_PATH = os.environ.get("TEST_LUA_WEB_PATH", "/lua")
TEST_ROOT = os.environ.get("TEST_NGINX_ROOT")


def request(path, method="GET", body=None, headers=None):
    status, body, _ = request_with_headers(path, method, body, headers)
    return status, body


def request_with_headers(path, method="GET", body=None, headers=None):
    deadline = time.time() + 5
    last_error = None

    while time.time() < deadline:
        conn = None
        try:
            conn = http.client.HTTPConnection(HOST, PORT, timeout=1)
            conn.request(method, path, body=body, headers=headers or {})
            response = conn.getresponse()
            body = response.read().decode()
            response_headers = {
                name.lower(): value for name, value in response.getheaders()
            }
            return response.status, body, response_headers

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


def count_error_log(pattern):
    if TEST_ROOT is None:
        raise AssertionError("TEST_NGINX_ROOT is not set")

    with open(os.path.join(TEST_ROOT, "logs", "error.log"), encoding="utf-8") as log:
        return sum(1 for line in log if pattern in line)


def run_tests(label, tests):
    try:
        for name, test in tests:
            test()
            print(f"ok - {name}")

    except Exception as exc:
        print(f"not ok - {label}: {exc}", file=sys.stderr)
        return 1

    return 0
