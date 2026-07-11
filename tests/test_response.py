import hashlib
import http.client
import socket
import threading
import time

from lua_web_test import (
    HOST,
    LUA_WEB_PATH,
    PORT,
    delayed_body_request,
    request_with_headers,
    run_tests,
)


BACKPRESSURE_BLOCK_SIZE = 64 * 1024
BACKPRESSURE_BLOCKS = 256
BACKPRESSURE_FILLER = bytes(range(256)) * 256


def backpressure_block(index):
    return index.to_bytes(8, "big") + BACKPRESSURE_FILLER[8:]


def test_response_headers_are_sent():
    status, _, headers = request_with_headers(LUA_WEB_PATH)

    if status != 201:
        raise AssertionError(f"expected 201, got {status}")

    if headers.get("content-type") != "text/plain":
        raise AssertionError("expected response content-type header")

    if headers.get("x-test") != "one":
        raise AssertionError("expected response x-test header")


def test_response_stream_waits_for_request_body():
    payload = "streamed request body"
    status, body = delayed_body_request("/lua-body-stream", payload)

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != payload:
        raise AssertionError(f"expected streamed request body, got {body!r}")


def test_response_resumes_after_downstream_backpressure():
    total = BACKPRESSURE_BLOCK_SIZE * BACKPRESSURE_BLOCKS
    expected = hashlib.sha256()

    for index in range(BACKPRESSURE_BLOCKS):
        expected.update(backpressure_block(index))

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 16384)
    sock.settimeout(5)

    sender = None
    response = None
    state = {"sent": 0, "error": None}

    try:
        sock.connect((HOST, PORT))
        request_head = (
            "POST /lua-body-stream HTTP/1.1\r\n"
            f"Host: {HOST}:{PORT}\r\n"
            f"Content-Length: {total}\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Connection: close\r\n"
            "\r\n"
        ).encode("ascii")
        sock.sendall(request_head)

        def send_body():
            try:
                for index in range(BACKPRESSURE_BLOCKS):
                    sock.sendall(backpressure_block(index))
                    state["sent"] += BACKPRESSURE_BLOCK_SIZE

            except OSError as exc:
                state["error"] = exc

        sender = threading.Thread(target=send_body, daemon=True)
        sender.start()

        response = http.client.HTTPResponse(sock, method="POST")
        response.begin()

        if response.status != 200:
            raise AssertionError(f"expected 200, got {response.status}")

        deadline = time.monotonic() + 5
        last_sent = -1
        stalled_since = None

        while time.monotonic() < deadline:
            sent = state["sent"]

            if not sender.is_alive():
                raise AssertionError(
                    "request body finished before downstream backpressure")

            if sent >= BACKPRESSURE_BLOCK_SIZE * 4 and sent == last_sent:
                if stalled_since is None:
                    stalled_since = time.monotonic()
                elif time.monotonic() - stalled_since >= 0.2:
                    break

            else:
                last_sent = sent
                stalled_since = None

            time.sleep(0.01)

        else:
            raise AssertionError("downstream backpressure was not observed")

        actual = hashlib.sha256()
        received = 0

        while True:
            chunk = response.read(BACKPRESSURE_BLOCK_SIZE)
            if not chunk:
                break

            actual.update(chunk)
            received += len(chunk)

        sender.join(2)

        if sender.is_alive():
            raise AssertionError("request body sender did not finish")

        if state["error"] is not None:
            raise AssertionError(f"request body send failed: {state['error']}")

        if state["sent"] != total:
            raise AssertionError(
                f"expected to send {total} bytes, sent {state['sent']}")

        if received != total:
            raise AssertionError(
                f"expected {total} response bytes, received {received}")

        if actual.digest() != expected.digest():
            raise AssertionError("response body hash mismatch")

    finally:
        if response is not None:
            response.close()

        try:
            sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass

        sock.close()

        if sender is not None:
            sender.join(1)


def test_response_new():
    status, body, headers = request_with_headers("/lua-response-new")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "Response.new":
        raise AssertionError(f"expected Response.new body, got {body!r}")

    if headers.get("x-response-test") != "ok":
        raise AssertionError("expected Response.new header")


def main():
    return run_tests("Response API behavior", [
        ("Response headers are sent",
         test_response_headers_are_sent),
        ("response stream waits for request body",
         test_response_stream_waits_for_request_body),
        ("response resumes after downstream backpressure",
         test_response_resumes_after_downstream_backpressure),
        ("Response.new",
         test_response_new),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
