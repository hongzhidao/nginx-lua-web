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


def test_readable_stream_preserves_chunk_boundaries():
    status, body = request("/lua-stream-chunks")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "first:second:true":
        raise AssertionError(f"expected separate stream chunks, got {body!r}")


def test_readable_stream_drains_chunk_before_error():
    status, body = request("/lua-stream-error")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "true:before error:false":
        raise AssertionError(f"expected buffered chunk before error, got {body!r}")


def main():
    return run_tests("ReadableStream API behavior", [
        ("ReadableStream.new and controller enqueue",
         test_readable_stream_new_and_controller_enqueue),
        ("ReadableStream pull source",
         test_readable_stream_pull_source),
        ("ReadableStream preserves chunk boundaries",
         test_readable_stream_preserves_chunk_boundaries),
        ("ReadableStream drains chunk before error",
         test_readable_stream_drains_chunk_before_error),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
