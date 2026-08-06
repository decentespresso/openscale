#!/usr/bin/env python3

import argparse
import os
import sys
import termios
import time
from datetime import datetime

BAUD_CONSTANTS = {
    name[1:]: getattr(termios, name)
    for name in ("B9600", "B115200", "B230400", "B460800", "B921600")
    if hasattr(termios, name)
}


def open_serial(port, baud):
    speed = BAUD_CONSTANTS.get(str(baud))
    if speed is None:
        sys.exit(f"unsupported baud {baud}; pick one of {sorted(BAUD_CONSTANTS)}")
    fd = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    iflag, oflag, cflag, lflag, _ispeed, _ospeed, cc = termios.tcgetattr(fd)
    iflag = 0
    oflag = 0
    cflag = termios.CS8 | termios.CREAD | termios.CLOCAL
    lflag = 0
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
