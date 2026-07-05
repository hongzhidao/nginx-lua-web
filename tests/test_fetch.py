import os

from lua_web_test import count_error_log, delayed_body_request, request, run_tests


HAVE_HTTP_SSL = os.environ.get("TEST_NGINX_HAVE_HTTP_SSL") == "1"


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


def test_fetch_https_returns_response():
    if not HAVE_HTTP_SSL:
        return

    status, body = request("/lua-fetch-https")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch HTTPS response":
        raise AssertionError(f"expected HTTPS response body, got {body!r}")


def test_fetch_https_verifies_certificates_by_default():
    if not HAVE_HTTP_SSL:
        return

    status, body = request("/lua-fetch-https-verify-fail")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch HTTPS verify failed":
        raise AssertionError(f"expected HTTPS verification failure, got {body!r}")


def test_fetch_opens_tcp_connection():
    connected = count_error_log("fetch connected")
    min_connected = 3 if HAVE_HTTP_SSL else 2
    max_connected = 4 if HAVE_HTTP_SSL else 3

    if connected < min_connected:
        raise AssertionError("expected fetch to open TCP connections")

    if connected > max_connected:
        raise AssertionError("expected fetch keepalive to limit TCP connects")


def test_fetch_completes_ssl_handshake():
    if not HAVE_HTTP_SSL:
        return

    if count_error_log("fetch SSL handshake completed") < 1:
        raise AssertionError("expected fetch to complete an SSL handshake")


def test_fetch_reuses_tcp_connection():
    if count_error_log("fetch keepalive connection reused") < 2:
        raise AssertionError("expected fetch to reuse TCP connections")


def test_fetch_reads_response_header():
    if count_error_log("fetch response header received") < 4:
        raise AssertionError("expected fetch to read response headers")


def test_fetch_sends_request_body():
    if count_error_log("fetch response status:") < 4:
        raise AssertionError("expected fetch to send request bodies")


def test_fetch_read_timeout_option():
    status, body = request("/lua-fetch-timeout")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch response header read timed out":
        raise AssertionError(f"expected fetch read timeout, got {body!r}")


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
        ("fetch HTTPS returns response",
         test_fetch_https_returns_response),
        ("fetch HTTPS verifies certificates by default",
         test_fetch_https_verifies_certificates_by_default),
        ("fetch opens TCP connection",
         test_fetch_opens_tcp_connection),
        ("fetch completes SSL handshake",
         test_fetch_completes_ssl_handshake),
        ("fetch reuses TCP connection",
         test_fetch_reuses_tcp_connection),
        ("fetch reads response header",
         test_fetch_reads_response_header),
        ("fetch sends request body",
         test_fetch_sends_request_body),
        ("fetch read timeout option",
         test_fetch_read_timeout_option),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
