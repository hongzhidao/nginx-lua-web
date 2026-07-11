import hashlib
import os
import socket
import sys
import time
from pathlib import Path


PHASE_ONE_BYTES = 4 * 1024 * 1024
WAIT_TIMEOUT = 30


class SocketReader:
    def __init__(self, conn):
        self.conn = conn
        self.buffer = bytearray()

    def read_exactly(self, size):
        while len(self.buffer) < size:
            chunk = self.conn.recv(max(4096, size - len(self.buffer)))
            if not chunk:
                raise EOFError("connection closed while reading request")
            self.buffer.extend(chunk)

        data = bytes(self.buffer[:size])
        del self.buffer[:size]
        return data

    def read_line(self):
        while True:
            end = self.buffer.find(b"\r\n")
            if end != -1:
                line = bytes(self.buffer[:end])
                del self.buffer[:end + 2]
                return line

            if len(self.buffer) > 8192:
                raise ValueError("request line is too large")

            chunk = self.conn.recv(4096)
            if not chunk:
                raise EOFError("connection closed while reading line")
            self.buffer.extend(chunk)


def write_marker(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(str(value), encoding="utf-8")
    os.replace(temporary, path)


def wait_for(path):
    deadline = time.monotonic() + WAIT_TIMEOUT

    while time.monotonic() < deadline:
        if path.exists():
            return
        time.sleep(0.01)

    raise TimeoutError(f"timed out waiting for {path.name}")


def handle_request(conn, root):
    conn.settimeout(WAIT_TIMEOUT)
    reader = SocketReader(conn)

    request_line = reader.read_line()
    if not request_line:
        return

    headers = {}
    while True:
        line = reader.read_line()
        if not line:
            break

        name, separator, value = line.partition(b":")
        if separator != b":":
            raise ValueError("invalid request header")
        headers[name.strip().lower()] = value.strip().lower()

    if headers.get(b"transfer-encoding") != b"chunked":
        raise ValueError("expected a chunked request body")

    digest = hashlib.sha256()
    received = 0
    phase_one_written = False

    while True:
        size_line = reader.read_line().split(b";", 1)[0]
        size = int(size_line, 16)

        if size == 0:
            while reader.read_line():
                pass
            break

        chunk = reader.read_exactly(size)
        if reader.read_exactly(2) != b"\r\n":
            raise ValueError("invalid chunk terminator")

        digest.update(chunk)
        received += len(chunk)

        if not phase_one_written and received >= PHASE_ONE_BYTES:
            write_marker(root / "fetch-upload-phase-1", received)
            phase_one_written = True
            wait_for(root / "fetch-upload-release-1")

    write_marker(root / "fetch-upload-phase-2", received)
    wait_for(root / "fetch-upload-release-2")

    body = f"{received}:{digest.hexdigest()}".encode("ascii")
    conn.sendall(
        b"HTTP/1.1 200 OK\r\n"
        b"Content-Type: text/plain\r\n"
        b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n"
        b"Connection: close\r\n"
        b"\r\n" + body
    )


def main():
    port = int(sys.argv[1])
    root = Path(sys.argv[2])

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", port))
        server.listen(4)

        while True:
            conn, _ = server.accept()

            with conn:
                try:
                    handle_request(conn, root)
                except (EOFError, OSError, TimeoutError, ValueError) as exc:
                    write_marker(root / "fetch-upload-sink-error", repr(exc))


if __name__ == "__main__":
    main()
