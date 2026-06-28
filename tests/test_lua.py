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
    return request_url(TEST_URL)


def request_url(url):
    try:
        with urlopen(url, timeout=1) as response:
            return response.status, response.read()
    except HTTPError as error:
        return error.code, error.read()
    except URLError:
        return None, None


def remove_marker():
    if TEST_MARKER is not None and os.path.exists(TEST_MARKER):
        os.unlink(TEST_MARKER)


def decode_chunked(body):
    decoded = b""

    while body:
        size_line, _, rest = body.partition(b"\r\n")
        size = int(size_line.split(b";", 1)[0], 16)
        if size == 0:
            return decoded

        decoded += rest[:size]
        body = rest[size + 2:]

    return decoded


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

    response_head, _, response_body = response.partition(b"\r\n\r\n")
    header_lines = response_head.split(b"\r\n")
    status_line = header_lines[0]
    status = int(status_line.split()[1])

    if any(line.lower() == b"transfer-encoding: chunked"
           for line in header_lines[1:]):
        response_body = decode_chunked(response_body)

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
        print("GET /hello returns Lua stream response")

        if TEST_MARKER is not None and os.path.exists(TEST_MARKER):
            with open(TEST_MARKER, "rb") as marker:
                marker_body = marker.read()

            if marker_body != b"\n" + STREAM_BODY:
                print("GET /hello expected empty body marker, "
                      f"got {marker_body!r}")
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

                    missing_url = TEST_URL.rsplit("/", 1)[0] + "/missing"
                    status, body = request_url(missing_url)
                    if status != 404:
                        print(f"GET /missing expected 404, got {status} "
                              f"{body!r}")
                        print("FAIL")
                        return 1

                    print("GET /missing returns 404 for non-stream "
                          "handler result")

                    remove_marker()
                    mirror_url = TEST_URL.rsplit("/", 1)[0] + "/mirror"
                    status, body = request_url(mirror_url)
                    if status != 200:
                        print(f"GET /mirror expected 200, got {status} "
                              f"{body!r}")
                        print("FAIL")
                        return 1

                    if os.path.exists(TEST_MARKER):
                        print("GET /mirror subrequest unexpectedly ran "
                              "Lua handler")
                        print("FAIL")
                        return 1

                    print("Lua handler rejects subrequests")
                    print("OK")
                    return 0

                expected_marker = REQUEST_BODY + b"\n" + STREAM_BODY
                print(f"POST body marker expected {expected_marker!r}, "
                      f"got {marker_body!r}")
                print("FAIL")
                return 1

            print(f"POST /hello expected 200 {EXPECTED_BODY!r}, "
                  f"got {status} {body!r}")
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
