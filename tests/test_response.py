from lua_web_test import (
    LUA_WEB_PATH,
    delayed_body_request,
    request_with_headers,
    run_tests,
)


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
        ("Response.new",
         test_response_new),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
