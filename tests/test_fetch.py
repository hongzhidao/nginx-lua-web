import hashlib
import http.client
import os
import threading
import time

from lua_web_test import (
    HOST,
    PORT,
    TEST_ROOT,
    count_error_log,
    delayed_body_request,
    request,
    run_tests,
)


HAVE_HTTP_SSL = os.environ.get("TEST_NGINX_HAVE_HTTP_SSL") == "1"
UPLOAD_CHUNK = bytes(range(256)) * 256
UPLOAD_SIZE = 32 * 1024 * 1024


def test_fetch_returns_response_with_body():
    payload = "delayed fetch body"
    status, body = delayed_body_request("/lua-fetch", payload)

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch request response":
        raise AssertionError(f"expected fetch response body, got {body!r}")


def test_fetch_buffers_response_body_while_caller_is_yielded():
    payload = "release fetch body"
    status, body = delayed_body_request("/lua-fetch-body-after-yield", payload)

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "262144:xxx:release fetch body":
        raise AssertionError(f"expected buffered fetch body, got {body!r}")


def test_fetch_no_body_response_has_nil_body():
    status, body = request("/lua-fetch-no-body")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch response body nil":
        raise AssertionError(f"expected nil fetch response body, got {body!r}")


def test_fetch_head_response_has_nil_body():
    status, body = request("/lua-fetch-head")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch HEAD body nil":
        raise AssertionError(f"expected nil fetch HEAD body, got {body!r}")


def test_fetch_dns_failure_returns_error():
    status, body = request("/lua-fetch-dns-fail")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch DNS resolve failed":
        raise AssertionError(f"expected DNS failure error, got {body!r}")


def test_fetch_https_returns_response():
    if not HAVE_HTTP_SSL:
        return

    status, body = request("/lua-fetch-https")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch HTTPS response":
        raise AssertionError(f"expected HTTPS response body, got {body!r}")


def test_fetch_https_verifies_certificates_by_default():
    if not HAVE_HTTP_SSL:
        return

    status, body = request("/lua-fetch-https-verify-fail")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch HTTPS verify failed":
        raise AssertionError(f"expected HTTPS verification failure, got {body!r}")


def test_fetch_opens_tcp_connection():
    connected = count_error_log("fetch connected")
    min_connected = 3 if HAVE_HTTP_SSL else 2
    max_connected = 4 if HAVE_HTTP_SSL else 3

    if connected < min_connected:
        raise AssertionError("expected fetch to open TCP connections")

    if connected > max_connected:
        raise AssertionError("expected fetch keepalive to limit TCP connects")


def test_fetch_completes_ssl_handshake():
    if not HAVE_HTTP_SSL:
        return

    if count_error_log("fetch SSL handshake completed") < 1:
        raise AssertionError("expected fetch to complete an SSL handshake")


def test_fetch_reuses_tcp_connection():
    if count_error_log("fetch keepalive connection reused") < 2:
        raise AssertionError("expected fetch to reuse TCP connections")


def test_fetch_reads_response_header():
    if count_error_log("fetch response header received") < 4:
        raise AssertionError("expected fetch to read response headers")


def test_fetch_sends_request_body():
    if count_error_log("fetch response status:") < 4:
        raise AssertionError("expected fetch to send request bodies")


def test_fetch_sends_request_headers():
    status, body = request("/lua-fetch-headers")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch sent request headers":
        raise AssertionError(f"expected fetch request headers, got {body!r}")


def test_fetch_request_input_accepts_init():
    status, body = request("/lua-fetch-request-init")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch Request init merged":
        raise AssertionError(f"expected fetch Request init merge, got {body!r}")


def test_fetch_response_body_methods():
    status, body = request("/lua-fetch-body-mixin")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch json response:fetch text response":
        raise AssertionError(f"expected fetch body methods, got {body!r}")


def test_fetch_read_timeout_option():
    status, body = request("/lua-fetch-timeout")

    if status != 200:
        raise AssertionError(f"expected 200, got {status}: {body!r}")

    if body != "fetch response header read timed out":
        raise AssertionError(f"expected fetch read timeout, got {body!r}")


def wait_for_file(path, upload, error_path, result, timeout=15):
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        if os.path.exists(path):
            return

        if os.path.exists(error_path):
            with open(error_path, encoding="utf-8") as error_file:
                raise AssertionError(
                    f"fetch upload sink failed: {error_file.read()}")

        if not upload.is_alive():
            raise AssertionError(
                f"upload ended before reaching memory phase: {result!r}")

        time.sleep(0.02)

    raise AssertionError(f"timed out waiting for {os.path.basename(path)}")


def nginx_worker_rss():
    if TEST_ROOT is None or not os.path.isdir("/proc"):
        raise AssertionError("worker RSS test requires Linux /proc")

    with open(os.path.join(TEST_ROOT, "logs", "nginx.pid"),
              encoding="ascii") as pid_file:
        master = int(pid_file.read().strip())

    children_path = f"/proc/{master}/task/{master}/children"
    with open(children_path, encoding="ascii") as children_file:
        children = children_file.read().split()

    if len(children) != 1:
        raise AssertionError(f"expected one nginx worker, got {children!r}")

    with open(f"/proc/{children[0]}/status", encoding="ascii") as status:
        for line in status:
            if line.startswith("VmRSS:"):
                return int(line.split()[1]) * 1024

    raise AssertionError("nginx worker VmRSS is unavailable")


def test_fetch_upload_memory_stays_bounded():
    if not os.path.isdir("/proc"):
        return

    phase_one = os.path.join(TEST_ROOT, "fetch-upload-phase-1")
    phase_two = os.path.join(TEST_ROOT, "fetch-upload-phase-2")
    release_one = os.path.join(TEST_ROOT, "fetch-upload-release-1")
    release_two = os.path.join(TEST_ROOT, "fetch-upload-release-2")
    sink_error = os.path.join(TEST_ROOT, "fetch-upload-sink-error")
    result = {}

    for path in (phase_one, phase_two, release_one, release_two, sink_error):
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass

    def upload_body():
        conn = http.client.HTTPConnection(HOST, PORT, timeout=30)

        def chunks():
            for _ in range(UPLOAD_SIZE // len(UPLOAD_CHUNK)):
                yield UPLOAD_CHUNK

        try:
            conn.request(
                "POST",
                "/lua-fetch-upload",
                body=chunks(),
                headers={"Content-Type": "application/octet-stream"},
                encode_chunked=True,
            )
            response = conn.getresponse()
            result["status"] = response.status
            result["body"] = response.read().decode("ascii")

        except Exception as exc:
            result["error"] = exc

        finally:
            conn.close()

    upload = threading.Thread(target=upload_body, daemon=True)
    upload.start()

    try:
        wait_for_file(phase_one, upload, sink_error, result)
        time.sleep(0.2)
        rss_at_phase_one = nginx_worker_rss()

        open(release_one, "a", encoding="ascii").close()
        wait_for_file(phase_two, upload, sink_error, result)
        time.sleep(0.2)
        rss_at_phase_two = nginx_worker_rss()

    finally:
        open(release_one, "a", encoding="ascii").close()
        open(release_two, "a", encoding="ascii").close()
        upload.join(30)

    if upload.is_alive():
        raise AssertionError("fetch upload did not finish")

    if "error" in result:
        raise AssertionError(f"fetch upload failed: {result['error']}")

    if result.get("status") != 200:
        raise AssertionError(
            f"expected upload status 200, got {result.get('status')}: "
            f"{result.get('body')!r}")

    digest = hashlib.sha256()
    for _ in range(UPLOAD_SIZE // len(UPLOAD_CHUNK)):
        digest.update(UPLOAD_CHUNK)

    expected = f"{UPLOAD_SIZE}:{digest.hexdigest()}"
    if result.get("body") != expected:
        raise AssertionError(f"unexpected upload result: {result.get('body')!r}")

    growth = rss_at_phase_two - rss_at_phase_one
    if growth >= 12 * 1024 * 1024:
        raise AssertionError(
            f"fetch upload grew worker RSS by {growth / (1024 * 1024):.1f} MiB")


def main():
    return run_tests("fetch API behavior", [
        ("fetch returns Response with body",
         test_fetch_returns_response_with_body),
        ("fetch buffers response body while caller is yielded",
         test_fetch_buffers_response_body_while_caller_is_yielded),
        ("fetch no-body response has nil body",
         test_fetch_no_body_response_has_nil_body),
        ("fetch HEAD response has nil body",
         test_fetch_head_response_has_nil_body),
        ("fetch DNS failure returns error",
         test_fetch_dns_failure_returns_error),
        ("fetch HTTPS returns response",
         test_fetch_https_returns_response),
        ("fetch HTTPS verifies certificates by default",
         test_fetch_https_verifies_certificates_by_default),
        ("fetch opens TCP connection",
         test_fetch_opens_tcp_connection),
        ("fetch completes SSL handshake",
         test_fetch_completes_ssl_handshake),
        ("fetch reuses TCP connection",
         test_fetch_reuses_tcp_connection),
        ("fetch reads response header",
         test_fetch_reads_response_header),
        ("fetch sends request body",
         test_fetch_sends_request_body),
        ("fetch sends request headers",
         test_fetch_sends_request_headers),
        ("fetch Request input accepts init",
         test_fetch_request_input_accepts_init),
        ("fetch Response body methods",
         test_fetch_response_body_methods),
        ("fetch read timeout option",
         test_fetch_read_timeout_option),
        ("fetch upload memory stays bounded",
         test_fetch_upload_memory_stays_bounded),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
