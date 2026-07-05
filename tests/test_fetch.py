from lua_web_test import count_error_log, delayed_body_request, request, run_tests


def test_fetch_returns_response_with_body():
    payload = "delayed fetch body"
    status, body = delayed_body_request("/lua-fetch", payload)

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch request response":
        raise AssertionError(f"expected fetch response body, got {body!r}")


def test_fetch_buffers_response_body_while_caller_is_yielded():
    payload = "release fetch body"
    status, body = delayed_body_request("/lua-fetch-body-after-yield", payload)

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "262144:xxx:release fetch body":
        raise AssertionError(f"expected buffered fetch body, got {body!r}")


def test_fetch_no_body_response_has_nil_body():
    status, body = request("/lua-fetch-no-body")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch response body nil":
        raise AssertionError(f"expected nil fetch response body, got {body!r}")


def test_fetch_head_response_has_nil_body():
    status, body = request("/lua-fetch-head")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch HEAD body nil":
        raise AssertionError(f"expected nil fetch HEAD body, got {body!r}")


def test_fetch_dns_failure_returns_error():
    status, body = request("/lua-fetch-dns-fail")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch DNS resolve failed":
        raise AssertionError(f"expected DNS failure error, got {body!r}")


def test_fetch_opens_tcp_connection():
    connected = count_error_log("fetch connected")

    if connected < 2:
        raise AssertionError("expected fetch to open TCP connections")

    if connected > 3:
        raise AssertionError("expected fetch keepalive to limit TCP connects")


def test_fetch_reuses_tcp_connection():
    if count_error_log("fetch keepalive connection reused") < 2:
        raise AssertionError("expected fetch to reuse TCP connections")


def test_fetch_reads_response_header():
    if count_error_log("fetch response header received") < 4:
        raise AssertionError("expected fetch to read response headers")


def test_fetch_sends_request_body():
    if count_error_log("fetch response status:") < 4:
        raise AssertionError("expected fetch to send request bodies")


def main():
    return run_tests("fetch API behavior", [
        ("fetch returns Response with body",
         test_fetch_returns_response_with_body),
        ("fetch buffers response body while caller is yielded",
         test_fetch_buffers_response_body_while_caller_is_yielded),
        ("fetch no-body response has nil body",
         test_fetch_no_body_response_has_nil_body),
        ("fetch HEAD response has nil body",
         test_fetch_head_response_has_nil_body),
        ("fetch DNS failure returns error",
         test_fetch_dns_failure_returns_error),
        ("fetch opens TCP connection",
         test_fetch_opens_tcp_connection),
        ("fetch reuses TCP connection",
         test_fetch_reuses_tcp_connection),
        ("fetch reads response header",
         test_fetch_reads_response_header),
        ("fetch sends request body",
         test_fetch_sends_request_body),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
