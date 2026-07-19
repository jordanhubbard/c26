#!/usr/bin/env python3
"""Scripted TCP peer for the c26 network gate.

QEMU's user-net `guestfwd` runs this command when the guest opens a TCP
connection to the guestfwd address; the guest's TCP stream is this process's
stdin/stdout, with QEMU's own (standards-compliant) TCP terminating the
guest side. So this exercises the c26 TCP client's handshake, data transfer,
and close against a real peer — deterministically, with no external network.

Protocol: read whatever the guest sends. An HTTP GET (from the FETCH app) gets
a canned HTTP/1.0 response whose body carries a marker; anything else gets the
plain C26-TCP-OK echo (the TCP round-trip test). Then close (FIN to the guest).
"""
import os
import sys


def main() -> int:
    stdin = sys.stdin.buffer
    stdout = sys.stdout.buffer

    # Read one chunk of request from the guest (it sends a short payload).
    # Use a single os.read, not read(64): the guest sends fewer than 64 bytes
    # then waits for our reply before closing, so a full-64-byte read would
    # deadlock (it blocks until 64 bytes or EOF, which only arrives on close).
    try:
        os.set_blocking(stdin.fileno(), True)
    except (OSError, ValueError):
        pass
    request = os.read(stdin.fileno(), 256)
    text = request.decode("ascii", "replace")

    if text.startswith("GET "):
        body = b"C26-FETCH-OK hello from the scripted peer\n"
        stdout.write(b"HTTP/1.0 200 OK\r\n")
        stdout.write(b"Content-Type: text/plain\r\n")
        stdout.write(b"Content-Length: " + str(len(body)).encode() + b"\r\n")
        stdout.write(b"\r\n")
        stdout.write(body)
    else:
        stdout.write(b"C26-TCP-OK ")
        stdout.write(text.strip().encode("ascii", "replace"))
        stdout.write(b"\n")
    stdout.flush()
    # Closing stdout sends FIN to the guest, so its recv sees EOF.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
