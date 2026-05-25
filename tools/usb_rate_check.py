#!/usr/bin/env python3
"""
USB scale-reading stress/verify for the Half Decent Scale.

Enables the 10 Hz USB binary weight stream and measures the delivered rate +
integrity over the serial link, the same way ws_rate_check.py does for WiFi.
Run alongside the WiFi load to stress both data paths at once.

Protocol (from include/usbcomm.h, include/ble.h, src/hds.ino):
  enable cmd : 03 20 01 <mult>   (interval = mult*100 ms; mult=1 => 10 Hz)
  weight frame (7 bytes): 03 CE wHi wLo 00 00 XOR   (XOR = data[0..5])
The binary frames are interleaved with ASCII debug on the same UART, so we sync
on the 03 CE header and validate the XOR.

NOTE: opening this CH343 port toggles the auto-reset line, so the scale reboots
when this starts; it waits for boot, then enables the stream. Dependency-free
(termios), like serial_tap.py.

Usage: python3 tools/usb_rate_check.py [/dev/cu.wchusbserial110] --seconds 60 --mult 1
"""

import argparse
import os
import termios
import time
from datetime import datetime


def ts():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def open_port(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = a[1] = a[3] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[4] = a[5] = termios.B115200
    a[6] = list(a[6])
    a[6][termios.VMIN] = 0
    a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?", default="/dev/cu.wchusbserial110")
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--mult", type=int, default=1, help="interval multiplier (1=100ms/10Hz)")
    ap.add_argument("--boot-wait", type=float, default=7.0)
    args = ap.parse_args()

    fd = open_port(args.port)
    print(f"[{ts()}] opened {args.port} (scale resets on open); waiting {args.boot_wait}s for boot")
    time.sleep(args.boot_wait)
    # Flush boot backlog.
    try:
        while os.read(fd, 4096):
            pass
    except (BlockingIOError, OSError):
        pass
    enable = bytes([0x03, 0x20, 0x01, args.mult & 0xFF])
    os.write(fd, enable)
    print(f"[{ts()}] sent enable {enable.hex()} (mult={args.mult}); measuring {args.seconds:.0f}s")

    buf = bytearray()
    frames = 0
    bad = 0
    arrivals = []
    weights = []
    started = time.monotonic()
    end = started + args.seconds
    last_log = started
    last_frames = 0
    while time.monotonic() < end:
        nowm = time.monotonic()
        if nowm - last_log >= 30:
            inst = (frames - last_frames) / (nowm - last_log)
            print(f"[{ts()}] USB ... frames={frames} bad={bad} (~{inst:.1f} Hz last {nowm-last_log:.0f}s)", flush=True)
            last_log = nowm
            last_frames = frames
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            time.sleep(0.005)
            continue
        if not chunk:
            time.sleep(0.005)
            continue
        now = time.monotonic()
        buf += chunk
        # Scan for 03 CE weight frames.
        i = 0
        while i + 7 <= len(buf):
            if buf[i] == 0x03 and buf[i + 1] == 0xCE:
                f = buf[i:i + 7]
                xor = 0
                for b in f[:6]:
                    xor ^= b
                if xor == f[6]:
                    frames += 1
                    arrivals.append(now)
                    weights.append((f[2] << 8) | f[3])
                    i += 7
                    continue
                else:
                    bad += 1
            i += 1
        del buf[:i]

    os.close(fd)
    dur = (arrivals[-1] - arrivals[0]) if len(arrivals) > 1 else 0
    hz = (frames - 1) / dur if dur > 0 else 0
    gaps = [(arrivals[k + 1] - arrivals[k]) * 1000 for k in range(len(arrivals) - 1)]
    def stat(xs):
        if not xs:
            return "n/a"
        s = sorted(xs)
        p = lambda q: s[min(len(s) - 1, int(q * len(s)))]
        return f"min={min(s):.0f} p50={p(.5):.0f} p95={p(.95):.0f} max={max(s):.0f}ms"
    big = sum(1 for g in gaps if g > 150)
    print(f"[{ts()}] USB frames={frames} bad_xor={bad} over {dur:.1f}s -> EFFECTIVE {hz:.2f} Hz")
    print(f"  inter-frame: {stat(gaps)}  (gaps>150ms: {big})")
    verdict = "GOOD ~10Hz" if 9.0 <= hz <= 11.0 else ("LOW" if frames else "NO FRAMES")
    print(f"  VERDICT: {verdict}")


if __name__ == "__main__":
    main()
