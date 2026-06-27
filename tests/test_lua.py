#!/usr/bin/env python3

import os
import socket
import time
from urllib.error import HTTPError, URLError
from urllib.request import urlopen
from urllib.parse import urlparse


TEST_URL = os.environ.get("TEST_URL", "http://127.0.0.1:18080/hello")
TEST_MARKER = os.environ.get("TEST_MARKER")
EXPECTED_BODY = b"hello world"
REQUEST_BODY = b"request body read through stream"
STREAM_BODY = b"start:pull"


def request():
    try:
        with urlopen(TEST_URL, timeout=1) as response:
            return response.status, response.read()
    except HTTPError as error:
        return error.code, error.read()
    except URLError:
        return None, None


def slow_post():
    url = urlparse(TEST_URL)
    host = url.hostname or "127.0.0.1"
    port = url.port or 80
    path = url.path or "/"
    midpoint = len(REQUEST_BODY) // 2

    request_head = (
        f"POST {path} HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        f"Content-Length: {len(REQUEST_BODY)}\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).encode()

    with socket.create_connection((host, port), timeout=2) as sock:
        sock.settimeout(2)
        sock.sendall(request_head + REQUEST_BODY[:midpoint])
        time.sleep(0.2)
        sock.sendall(REQUEST_BODY[midpoint:])

        response = b""
        while True:
            data = sock.recv(4096)
            if not data:
                break

            response += data

    status_line, _, response_body = response.partition(b"\r\n\r\n")
    status = int(status_line.split()[1])

    return status, response_body


def main():
    body = None
    status = None

    for _ in range(50):
        status, body = request()
        if status is not None:
            break

        time.sleep(0.1)

    if status == 200 and body == EXPECTED_BODY:
        print("GET /hello returns 200 hello world")

        if TEST_MARKER is not None and os.path.exists(TEST_MARKER):
            with open(TEST_MARKER, "rb") as marker:
                marker_body = marker.read()

            if marker_body != b"\n" + STREAM_BODY:
                print(f"GET /hello expected empty body marker, got {marker_body!r}")
                print("FAIL")
                return 1

            print("Lua handler read empty GET body")

            status, body = slow_post()
            if status == 200 and body == EXPECTED_BODY:
                with open(TEST_MARKER, "rb") as marker:
                    marker_body = marker.read()

                if marker_body == REQUEST_BODY + b"\n" + STREAM_BODY:
                    print("POST /hello streams request body to Lua")
                    print("Lua Stream.new supports start and pull")
                    print("OK")
                    return 0

                expected_marker = REQUEST_BODY + b"\n" + STREAM_BODY
                print(f"POST body marker expected {expected_marker!r}, got {marker_body!r}")
                print("FAIL")
                return 1

            print(f"POST /hello expected 200 {EXPECTED_BODY!r}, got {status} {body!r}")
            print("FAIL")
            return 1

        print(f"Lua handler marker not found: {TEST_MARKER}")
        print("FAIL")
        return 1

    print(f"GET /hello expected 200 {EXPECTED_BODY!r}, got {status} {body!r}")
    print("FAIL")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
