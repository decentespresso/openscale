#!/usr/bin/env python3
"""
Timestamp and log lines from the scale's USB serial console.

Dependency-free (no pyserial): configures the line discipline with `stty` and
reads the `/dev/cu.*` callout device directly. Opening the callout device does
NOT toggle DTR/RTS, so the ESP32 keeps running and is not reset mid-test --
which matters when correlating scale-side logs with a live WebSocket session.

Usage:
    python3 tools/serial_tap.py                                  # /dev/cu.wchusbserial110 @ 115200
    python3 tools/serial_tap.py /dev/cu.wchusbserial110 --out /tmp/hds_serial.log
    python3 tools/serial_tap.py --grep "client"                  # only matching lines (case-insensitive)

Timestamps are local wall-clock (HH:MM:SS.mmm) so they line up with
ws_drop_repro.py output.
"""

import argparse
import os
import sys
import termios
import time
from datetime import datetime

# Not every baud constant exists on every platform (macOS termios lacks
# B460800/B921600), so build the table from whatever this termios actually has.
BAUD_CONSTANTS = {
    name[1:]: getattr(termios, name)
    for name in ("B9600", "B115200", "B230400", "B460800", "B921600")
    if hasattr(termios, name)
}


def open_serial(port, baud):
    """Open the callout device and set raw NN8N1 via termios on the SAME fd.

    Setting the line discipline on the same descriptor we read from avoids the
    trap where an out-of-process `stty` is undone by a fresh open(). O_RDONLY +
    CLOCAL + cleared HUPCL keep DTR/RTS from toggling, so the ESP32 isn't reset.
    """
    speed = BAUD_CONSTANTS.get(str(baud))
    if speed is None:
        sys.exit(f"unsupported baud {baud}; pick one of {sorted(BAUD_CONSTANTS)}")
    fd = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    iflag, oflag, cflag, lflag, _ispeed, _ospeed, cc = termios.tcgetattr(fd)
    iflag = 0
    oflag = 0
    cflag = termios.CS8 | termios.CREAD | termios.CLOCAL  # 8N1, reader on, ignore modem ctrl, no HUPCL
    lflag = 0  # raw: no canon/echo/signals
    cc = list(cc)
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, [iflag, oflag, cflag, lflag, speed, speed, cc])
    return fd


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?", default="/dev/cu.wchusbserial110")
    ap.add_argument("--baud", default="115200")
    ap.add_argument("--out", default=None, help="also append timestamped lines to this file")
    ap.add_argument("--grep", default=None, help="only print lines containing this substring (case-insensitive)")
    ap.add_argument("--printable", action="store_true",
                    help="strip non-printable bytes and skip lines with no readable text "
                         "(cuts through the binary Decent-Scale protocol to surface debug logs)")
    args = ap.parse_args()

    out = open(args.out, "a") if args.out else None
    fd = open_serial(args.port, args.baud)
    buf = b""
    print(f"[{datetime.now().strftime('%H:%M:%S.%f')[:-3]}] --- serial tap on {args.port} @ {args.baud} ---",
          flush=True)
    try:
        while True:
            try:
                chunk = os.read(fd, 4096)
            except BlockingIOError:
                time.sleep(0.02)
                continue
            if not chunk:
                time.sleep(0.02)
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                if args.printable:
                    # Keep only printable ASCII; the firmware's debug prints are
                    # ASCII while the steady-state weight protocol is binary.
                    kept = bytes(b for b in line if 32 <= b < 127 or b in (9,)).decode("ascii", "replace").strip()
                    if not kept or not any(c.isalnum() for c in kept):
                        continue
                    text = kept
                else:
                    text = line.decode("utf-8", "replace").rstrip("\r")
                if args.grep and args.grep.lower() not in text.lower():
                    continue
                stamped = f"[{datetime.now().strftime('%H:%M:%S.%f')[:-3]}] {text}"
                print(stamped, flush=True)
                if out:
                    out.write(stamped + "\n")
                    out.flush()
    except KeyboardInterrupt:
        pass
    finally:
        os.close(fd)
        if out:
            out.close()


if __name__ == "__main__":
    main()
