from lua_web_test import request, run_tests


def test_readable_stream_new_and_controller_enqueue():
    status, body = request("/lua-stream")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "hello from lua stream":
        raise AssertionError(f"expected lua stream body, got {body!r}")


def test_readable_stream_pull_source():
    status, body = request("/lua-stream-pull")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "pulled from source":
        raise AssertionError(f"expected pull stream body, got {body!r}")


def main():
    return run_tests("ReadableStream API behavior", [
        ("ReadableStream.new and controller enqueue",
         test_readable_stream_new_and_controller_enqueue),
        ("ReadableStream pull source",
         test_readable_stream_pull_source),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
