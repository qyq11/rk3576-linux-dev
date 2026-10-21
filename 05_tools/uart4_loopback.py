#!/usr/bin/env python3
"""Physical UART4 loopback on RK3576 Linux; uses only the standard library.

Connect 40-pin header pins 8 (TX) and 10 (RX) after checking the board pinout.
The existing three-pin debug console stays connected to the PC.
"""

import argparse
import os
import select
import sys
import time

PORT = "/dev/ttyS4"


def exchange(fd, payload, timeout):
    sent = 0
    received = bytearray()
    deadline = time.monotonic() + timeout
    while sent < len(payload):
        remaining = deadline - time.monotonic()
        if remaining <= 0 or not select.select([], [fd], [], remaining)[1]:
            raise TimeoutError("write timed out")
        try:
            count = os.write(fd, payload[sent:])
        except BlockingIOError:
            continue
        if count == 0:
            raise OSError("write returned zero bytes")
        sent += count

    deadline = time.monotonic() + timeout
    while len(received) < len(payload):
        remaining = deadline - time.monotonic()
        if remaining <= 0 or not select.select([fd], [], [], remaining)[0]:
            break
        try:
            received.extend(os.read(fd, 4096))
        except BlockingIOError:
            continue
    return bytes(received)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=int, default=100)
    args = parser.parse_args()
    if not 1 <= args.frames <= 9999:
        parser.error("--frames must be between 1 and 9999")
    if not sys.platform.startswith("linux"):
        parser.error("Run this script on the RK3576 Linux board, not Windows")

    import fcntl
    import termios

    fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    saved = None
    sent_bytes = received_bytes = matched = 0
    try:
        fcntl.ioctl(fd, termios.TIOCEXCL)
        saved = termios.tcgetattr(fd)
        settings = termios.tcgetattr(fd)
        settings[0] = 0  # No software flow control or input translation.
        settings[1] = 0  # No output translation.
        settings[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
        settings[3] = 0  # Raw input; no terminal echo.
        settings[4] = settings[5] = termios.B115200
        settings[6][termios.VMIN] = settings[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, settings)
        termios.tcflush(fd, termios.TCIOFLUSH)

        print(f"Port: {PORT}; settings: 115200 8N1; flow control: none")
        for sequence in range(1, args.frames + 1):
            payload = f"UART_TEST_{sequence:04d}\r\n".encode("ascii")
            received = exchange(fd, payload, timeout=1.0)
            sent_bytes += len(payload)
            received_bytes += len(received)
            if received != payload:
                print(f"Mismatch at frame {sequence}")
                print(f"Expected: {payload!r}")
                print(f"Received: {received!r}")
                break
            matched += 1

        extra = b""
        if matched == args.frames and select.select([fd], [], [], 0.1)[0]:
            extra = os.read(fd, 4096)
            received_bytes += len(extra)
        if extra:
            print(f"Unexpected trailing bytes: {extra!r}")
        passed = matched == args.frames and not extra
        print(f"Sent bytes: {sent_bytes}")
        print(f"Received bytes: {received_bytes}")
        print(f"Matched frames: {matched}/{args.frames}")
        print("Result: PASS" if passed else "Result: FAIL")
        return 0 if passed else 1
    finally:
        try:
            if saved is not None:
                termios.tcsetattr(fd, termios.TCSANOW, saved)
        finally:
            os.close(fd)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, TimeoutError) as error:
        print(f"Result: ERROR: {error}", file=sys.stderr)
        sys.exit(2)
