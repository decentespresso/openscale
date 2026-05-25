#!/usr/bin/env python3
"""
Fine-grained TCP reachability probe with wall-clock timestamps.

Runs alongside ws_drop_repro.py to answer one question when a WebSocket drop
occurs: was the scale still on the network, or did its WiFi fall off?

  * Scale stays reachable through the drop  -> app/TCP-layer close (e.g. the
    AsyncTCP ACK timeout) while WiFi is up.
  * Reachability also fails around the drop -> the scale's WiFi itself dropped
    (coexistence deauth / reassociation).

Opens a short-lived TCP connection to host:port each interval (default the HTTP
server on :80) and logs latency or failure. No root required (unlike ICMP at
sub-second intervals or tcpdump).

Usage:
    python3 tools/reach_probe.py                       # hds.local:80 every 0.4s
    python3 tools/reach_probe.py hds.local --interval 0.3 --port 80
"""

import argparse
import socket
import time
from datetime import datetime


def ts():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host", nargs="?", default="hds.local")
    ap.add_argument("--port", type=int, default=80)
    ap.add_argument("--interval", type=float, default=0.4)
    ap.add_argument("--timeout", type=float, default=2.0)
    args = ap.parse_args()

    # Resolve once so a flaky mDNS lookup isn't mistaken for the host vanishing.
    try:
        ip = socket.gethostbyname(args.host)
    except OSError as e:
        ip = args.host
        print(f"[{ts()}] WARN could not resolve {args.host}: {e}")
    print(f"[{ts()}] probing {args.host} ({ip}):{args.port} every {args.interval}s")

    fails = 0
    while True:
        t0 = time.monotonic()
        try:
            s = socket.create_connection((ip, args.port), timeout=args.timeout)
            s.close()
            dt = (time.monotonic() - t0) * 1000
            if fails:
                print(f"[{ts()}] OK   {dt:6.0f} ms   (recovered after {fails} fail(s))")
                fails = 0
            elif dt > 500:
                print(f"[{ts()}] SLOW {dt:6.0f} ms")
            else:
                print(f"[{ts()}] OK   {dt:6.0f} ms")
        except Exception as e:  # noqa: BLE001 - any failure is a reachability signal
            fails += 1
            print(f"[{ts()}] FAIL ({fails})        {type(e).__name__}: {e}")
        elapsed = time.monotonic() - t0
        if elapsed < args.interval:
            time.sleep(args.interval - elapsed)


if __name__ == "__main__":
    main()
