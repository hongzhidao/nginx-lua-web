from lua_web_test import request, run_tests


def test_url_and_url_search_params_new():
    status, body = request("/lua-url")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "URL.new":
        raise AssertionError(f"expected URL.new body, got {body!r}")


def main():
    return run_tests("URL API behavior", [
        ("URL.new and URLSearchParams.new",
         test_url_and_url_search_params_new),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
