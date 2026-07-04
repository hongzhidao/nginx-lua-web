from lua_web_test import LUA_WEB_PATH, request, run_tests


def test_lua_web_file_returns_response():
    status, body = request(LUA_WEB_PATH)

    if status != 201:
        raise AssertionError(f"expected 201, got {status}")

    if body != "hello from lua handler":
        raise AssertionError(f"expected lua response body, got {body!r}")


def test_lua_web_file_keeps_location_refs_separate():
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


def test_coroutine_library_is_not_exposed():
    status, body = request("/lua-coroutine-disabled")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "coroutine disabled":
        raise AssertionError(f"expected coroutine disabled body, got {body!r}")


def main():
    return run_tests("lua_web_file behavior", [
        ("lua handler returns Response",
         test_lua_web_file_returns_response),
        ("location refs stay separate",
         test_lua_web_file_keeps_location_refs_separate),
        ("App.new rejects arguments",
         test_app_new_rejects_arguments),
        ("coroutine library is not exposed",
         test_coroutine_library_is_not_exposed),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
