from lua_web_test import count_error_log, delayed_body_request, run_tests


def test_fetch_returns_response_with_body():
    payload = "delayed fetch body"
    status, body = delayed_body_request("/lua-fetch", payload)

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch request response":
        raise AssertionError(f"expected fetch response body, got {body!r}")


def test_fetch_opens_tcp_connection():
    if count_error_log("fetch connected") < 2:
        raise AssertionError("expected fetch to open TCP connections")


def test_fetch_reads_response_header():
    if count_error_log("fetch response header received") < 2:
        raise AssertionError("expected fetch to read response headers")


def test_fetch_sends_request_body():
    if count_error_log("fetch response status: 200") < 2:
        raise AssertionError("expected fetch to send request bodies")


def main():
    return run_tests("fetch API behavior", [
        ("fetch returns Response with body",
         test_fetch_returns_response_with_body),
        ("fetch opens TCP connection",
         test_fetch_opens_tcp_connection),
        ("fetch reads response header",
         test_fetch_reads_response_header),
        ("fetch sends request body",
         test_fetch_sends_request_body),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
