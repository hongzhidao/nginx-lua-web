from lua_web_test import delayed_body_request, request, run_tests


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


def test_request_new():
    status, body = request("/lua-request-new")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "Request.new":
        raise AssertionError(f"expected constructor body, got {body!r}")


def main():
    return run_tests("Request API behavior", [
        ("request body reader reads body",
         test_request_body_readable_stream_reader_reads_body),
        ("request body reader yields until body arrives",
         test_request_body_reader_yields_until_body_arrives),
        ("Request.new",
         test_request_new),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
