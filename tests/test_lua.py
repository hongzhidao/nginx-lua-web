#!/usr/bin/env python3

import os
import time
from urllib.error import HTTPError, URLError
from urllib.request import urlopen


TEST_URL = os.environ.get("TEST_URL", "http://127.0.0.1:18080/hello")
TEST_MARKER = os.environ.get("TEST_MARKER")
EXPECTED_BODY = b"hello world"


def request():
    try:
        with urlopen(TEST_URL, timeout=1) as response:
            return response.status, response.read()
    except HTTPError as error:
        return error.code, error.read()
    except URLError:
        return None, None


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
            print("Lua handler ran")
            print("OK")
            return 0

        print(f"Lua handler marker not found: {TEST_MARKER}")
        print("FAIL")
        return 1

    print(f"GET /hello expected 200 {EXPECTED_BODY!r}, got {status} {body!r}")
    print("FAIL")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
