#!/usr/bin/env python3
"""Send one debug command to a Biscuit device over USB serial."""

from __future__ import annotations

import argparse
import sys
import time


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command",
        help="Command name, e.g. ping, status, sync, file-transfer, study, home, sleep, reboot",
    )
    parser.add_argument("-p", "--port", default="/dev/cu.usbserial-5B1F0123571", help="Serial port")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument("--timeout", type=float, default=2.0, help="Seconds to read the response")
    parser.add_argument("--open-delay", type=float, default=0.5, help="Seconds to wait after opening the port")
    parser.add_argument("--boot-grace", type=float, default=5.0, help="Seconds to wait if opening serial reset the device")
    parser.add_argument("--retry-interval", type=float, default=1.0, help="Seconds between command resends")
    parser.add_argument("--max-resends", type=int, default=3, help="Maximum command sends while waiting for a response")
    return parser.parse_args()


def open_serial(args: argparse.Namespace):
    import serial

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = args.baud
    ser.timeout = 0.1
    ser.write_timeout = 2
    # Set reset-control lines before open. Setting them only after open can
    # briefly toggle some USB-to-serial bridges and reset the ESP32.
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.dtr = False
    ser.rts = False
    return ser


def is_command_response(data: bytes) -> bool:
    return (
        b"OK:" in data
        or b"ERR:" in data
        or b"STATUS:" in data
        or b"SCREENSHOT_START:" in data
    )


def drain_startup(ser, args: argparse.Namespace) -> None:
    boot_seen = False
    deadline = time.monotonic() + args.open_delay
    while time.monotonic() < deadline:
        chunk = ser.read(4096)
        if not chunk:
            continue
        sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()
        if b"[M5PAPER]" in chunk or b"boot:" in chunk or b"HalSystem.begin" in chunk:
            boot_seen = True

    if not boot_seen:
        ser.reset_input_buffer()
        return

    deadline = time.monotonic() + args.boot_grace
    while time.monotonic() < deadline:
        chunk = ser.read(4096)
        if not chunk:
            continue
        sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()
        if b"Entering activity:" in chunk:
            time.sleep(0.2)
            ser.reset_input_buffer()
            return


def main() -> int:
    args = parse_args()
    try:
        import serial
    except ModuleNotFoundError:
        print("pyserial is missing; run this through scripts/device-command after PlatformIO is installed", file=sys.stderr)
        return 2

    command = args.command.strip().upper().replace("-", "_")
    if command and not command.startswith("CMD:"):
        command = f"CMD:{command}"
    command_bytes = (command + "\n").encode("utf-8")
    binary_response = command == "CMD:SCREENSHOT"

    with open_serial(args) as ser:
        drain_startup(ser, args)
        ser.write(command_bytes)
        ser.flush()
        sends = 1
        next_resend = time.monotonic() + args.retry_interval

        deadline = time.monotonic() + args.timeout
        saw_response = False
        drain_deadline: float | None = None
        while time.monotonic() < deadline:
            chunk = ser.read(4096)
            if chunk:
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
                if is_command_response(chunk):
                    saw_response = True
                    if not binary_response and drain_deadline is None:
                        drain_deadline = time.monotonic() + 0.35

            now = time.monotonic()
            if drain_deadline is not None and now >= drain_deadline:
                break

            if (
                not binary_response
                and not saw_response
                and sends < args.max_resends
                and now >= next_resend
            ):
                ser.write(command_bytes)
                ser.flush()
                sends += 1
                next_resend = now + args.retry_interval

    return 0 if saw_response or binary_response else 1


if __name__ == "__main__":
    raise SystemExit(main())
