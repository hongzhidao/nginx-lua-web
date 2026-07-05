import json

from lua_web_test import PORT, delayed_body_request, request, run_tests


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


def test_request_text_yields_until_body_arrives():
    payload = "delayed request text body"
    status, body = delayed_body_request("/lua-body-text", payload)

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != payload:
        raise AssertionError(f"expected request text body, got {body!r}")


def test_request_json_parses_body():
    status, body = request(
        "/lua-body-json",
        method="POST",
        body='{"ok":true,"count":2,"message":"hello"}',
        headers={"Content-Type": "application/json"},
    )

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if json.loads(body) != {"ok": True, "count": 2, "message": "hello"}:
        raise AssertionError(f"expected request JSON echo, got {body!r}")


def test_request_url_is_absolute():
    status, body = request(
        "/lua-request-url?x=1",
        headers={"Host": f"example.test:{PORT}"},
    )

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    expected = f"http://example.test:{PORT}/lua-request-url?x=1"
    if body != expected:
        raise AssertionError(f"expected request url {expected!r}, got {body!r}")


def test_request_new():
    status, body = request("/lua-request-new")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "Request.new":
        raise AssertionError(f"expected constructor body, got {body!r}")


def test_request_without_body_has_nil_body():
    status, body = request("/lua-request-no-body")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "request body nil":
        raise AssertionError(f"expected nil request body, got {body!r}")


def main():
    return run_tests("Request API behavior", [
        ("request body reader reads body",
         test_request_body_readable_stream_reader_reads_body),
        ("request body reader yields until body arrives",
         test_request_body_reader_yields_until_body_arrives),
        ("request text yields until body arrives",
         test_request_text_yields_until_body_arrives),
        ("request json parses body",
         test_request_json_parses_body),
        ("request url is absolute",
         test_request_url_is_absolute),
        ("Request.new",
         test_request_new),
        ("request without body has nil body",
         test_request_without_body_has_nil_body),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
