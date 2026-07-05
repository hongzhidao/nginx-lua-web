import time

from lua_web_test import LUA_WEB_PATH, count_error_log, request, run_tests


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


def test_coroutine_library_is_not_exposed():
    status, body = request("/lua-coroutine-disabled")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "coroutine disabled":
        raise AssertionError(f"expected coroutine disabled body, got {body!r}")


def test_lua_vm_is_shared_from_main_conf():
    status, body = request("/lua-vm-write")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "lua VM writer set":
        raise AssertionError(f"expected VM writer body, got {body!r}")

    status, body = request("/lua-vm-read")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "lua VM shared":
        raise AssertionError(f"expected shared VM body, got {body!r}")


def test_lua_web_file_subrequest_is_rejected():
    pattern = "lua_web_file does not support subrequests"
    before = count_error_log(pattern)

    status, body = request("/lua-subrequest-mirror")

    if status != 204:
        raise AssertionError(f"expected 204, got {status}: {body!r}")

    deadline = time.time() + 2
    while time.time() < deadline:
        if count_error_log(pattern) > before:
            return

        time.sleep(0.05)

    raise AssertionError("expected lua_web_file subrequest rejection log")


def main():
    return run_tests("Module behavior", [
        ("lua_web_file returns Response",
         test_lua_web_file_returns_response),
        ("lua_web_file keeps location refs separate",
         test_lua_web_file_keeps_location_refs_separate),
        ("coroutine library is not exposed",
         test_coroutine_library_is_not_exposed),
        ("Lua VM is shared from main conf",
         test_lua_vm_is_shared_from_main_conf),
        ("lua_web_file rejects subrequests",
         test_lua_web_file_subrequest_is_rejected),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
