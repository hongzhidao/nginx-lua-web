import json

from lua_web_test import request, request_with_headers, run_tests


def test_json_stringify_and_parse():
    status, body = request("/lua-json")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "JSON.stringify and JSON.parse":
        raise AssertionError(f"expected JSON API body, got {body!r}")


def test_response_json():
    status, body, headers = request_with_headers("/lua-json-response")

    if status != 201:
        raise AssertionError(f"expected 201, got {status}: {body!r}")

    if headers.get("content-type") != "application/json":
        raise AssertionError("expected default JSON content-type")

    if headers.get("x-json-test") != "ok":
        raise AssertionError("expected Response.json custom header")

    data = json.loads(body)

    if data != {
        "ok": True,
        "count": 3,
        "items": [1, "two", None],
        "message": "hello",
    }:
        raise AssertionError(f"expected Response.json body, got {data!r}")


def test_response_json_preserves_content_type():
    status, body, headers = request_with_headers(
        "/lua-json-response-custom-type"
    )

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if headers.get("content-type") != "application/problem+json":
        raise AssertionError("expected custom JSON content-type")

    if json.loads(body) != {"ok": True}:
        raise AssertionError(f"expected custom JSON body, got {body!r}")


def test_response_json_rejects_invalid_init():
    status, body = request("/lua-json-response-errors")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "Response.json rejects invalid init":
        raise AssertionError(f"expected Response.json error checks, got {body!r}")


def main():
    return run_tests("JSON API behavior", [
        ("JSON.stringify and JSON.parse",
         test_json_stringify_and_parse),
        ("Response.json",
         test_response_json),
        ("Response.json preserves content-type",
         test_response_json_preserves_content_type),
        ("Response.json rejects invalid init",
         test_response_json_rejects_invalid_init),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
