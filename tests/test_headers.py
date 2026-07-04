from lua_web_test import request, run_tests


def test_headers_new():
    status, body = request("/lua-headers-new")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "Headers.new":
        raise AssertionError(f"expected Headers.new body, got {body!r}")


def main():
    return run_tests("Headers API behavior", [
        ("Headers.new",
         test_headers_new),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
