from lua_web_test import LUA_WEB_PATH, request, run_tests


def test_app_file_returns_response():
    status, body = request(LUA_WEB_PATH)

    if status != 201:
        raise AssertionError(f"expected 201, got {status}")

    if body != "hello from lua handler":
        raise AssertionError(f"expected lua response body, got {body!r}")


def test_app_file_keeps_location_refs_separate():
    status, body = request("/lua-alt")

    if status != 202:
        raise AssertionError(f"expected 202, got {status}")

    if body != "hello from second lua handler":
        raise AssertionError(f"expected second lua response body, got {body!r}")


def test_app_new_rejects_arguments():
    status, body = request("/lua-app-args")

    if status != 203:
        raise AssertionError(f"expected 203, got {status}")

    if body != "App.new rejected arguments":
        raise AssertionError(f"expected App.new rejection body, got {body!r}")


def test_app_get_routes_get_requests():
    status, body = request("/lua-methods", method="GET")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}")

    if body != "GET GET":
        raise AssertionError(f"expected GET route body, got {body!r}")


def test_app_post_routes_post_requests():
    status, body = request("/lua-methods", method="POST")

    if status != 201:
        raise AssertionError(f"expected 201, got {status}")

    if body != "POST POST":
        raise AssertionError(f"expected POST route body, got {body!r}")


def test_app_method_mismatch_returns_404():
    status, body = request("/lua-methods-post-only", method="GET")

    if status != 404:
        raise AssertionError(f"expected 404, got {status}")

    if body != "":
        raise AssertionError(f"expected empty 404 body, got {body!r}")


def test_coroutine_library_is_not_exposed():
    status, body = request("/lua-coroutine-disabled")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "coroutine disabled":
        raise AssertionError(f"expected coroutine disabled body, got {body!r}")


def main():
    return run_tests("App behavior", [
        ("lua handler returns Response",
         test_app_file_returns_response),
        ("location refs stay separate",
         test_app_file_keeps_location_refs_separate),
        ("App.new rejects arguments",
         test_app_new_rejects_arguments),
        ("App:get routes GET requests",
         test_app_get_routes_get_requests),
        ("App:post routes POST requests",
         test_app_post_routes_post_requests),
        ("method mismatch returns 404",
         test_app_method_mismatch_returns_404),
        ("coroutine library is not exposed",
         test_coroutine_library_is_not_exposed),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
