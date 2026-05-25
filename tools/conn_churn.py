#!/usr/bin/env python3
"""
Connection-churn stressor for the Half Decent Scale TCP / WebSocket server.

Run this alongside ws_drop_repro.py (which holds the real /snapshot stream) to
test the hypothesis that *connection activity* -- new TCP/WebSocket connects and
disconnects -- triggers WS drops or server hangs on the scale's
AsyncWebServer/AsyncTCP stack.

Why churn is suspect on ESP32:
  * lwIP has a small fixed TCP PCB pool. Every closed connection lingers in
    TIME_WAIT; rapid open/close can exhaust PCBs and stall the whole server.
  * The WS handler now allows up to 8 clients and reaps the OLDEST via
    cleanupClients(8). If churn (esp. abrupt/half-open closes that aren't reaped
    until their own ACK timeout) piles up >8 clients, the oldest -- the real
    stream -- gets evicted.

Modes (combine freely); each worker repeats its action at --rate per second:
  --tcp    open a raw TCP connection to :80, hold, close
  --http   open, send "GET / HTTP/1.0", read a little, close
  --ws     open a SECOND ws://host/snapshot, hold, disconnect (churns WS clients)
  --rst    close abruptly (SO_LINGER 0 -> RST) leaving half-open server state

Usage:
    python3 tools/conn_churn.py hds.local --tcp --http --ws --rate 5 --workers 4 --duration 240
    python3 tools/conn_churn.py hds.local --ws --rst --rate 3 --duration 180   # pile up half-open WS clients

Logs a periodic counter line and a final summary. A rising "hang" count (connect
attempts that time out) is the smoking gun for server-wide stalls.
"""

import argparse
import socket
import struct
import sys
import threading
import time
from datetime import datetime

try:
    from websocket import create_connection
except ImportError:
    create_connection = None

RST_LINGER = struct.pack("ii", 1, 0)  # SO_LINGER on, timeout 0 -> RST on close


def ts():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


class Counters:
    def __init__(self):
        self.lock = threading.Lock()
        self.ok = 0
        self.fail = 0
        self.hang = 0
        self.by_mode = {}
        self.last_fail = ""

    def add(self, mode, result, detail=""):
        with self.lock:
            self.by_mode[mode] = self.by_mode.get(mode, 0) + 1
            if result == "ok":
                self.ok += 1
            elif result == "hang":
                self.hang += 1
                self.last_fail = f"{mode} hang: {detail}"
            else:
                self.fail += 1
                self.last_fail = f"{mode} fail: {detail}"


def do_tcp(host, port, hold, rst, timeout):
    t0 = time.monotonic()
    try:
        s = socket.create_connection((host, port), timeout=timeout)
        if rst:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, RST_LINGER)
        if hold > 0:
            time.sleep(hold)
        s.close()
        return ("ok", f"{(time.monotonic()-t0)*1000:.0f}ms")
    except (TimeoutError, socket.timeout):
        return ("hang", f"connect>{timeout}s")
    except OSError as e:
        return ("fail", f"{type(e).__name__}: {e}")


def do_http(host, port, hold, rst, timeout):
    t0 = time.monotonic()
    try:
        s = socket.create_connection((host, port), timeout=timeout)
        s.sendall(b"GET / HTTP/1.0\r\nHost: x\r\n\r\n")
        try:
            s.recv(256)
        except (TimeoutError, socket.timeout):
            pass
        if rst:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, RST_LINGER)
        if hold > 0:
            time.sleep(hold)
        s.close()
        return ("ok", f"{(time.monotonic()-t0)*1000:.0f}ms")
    except (TimeoutError, socket.timeout):
        return ("hang", f"connect>{timeout}s")
    except OSError as e:
        return ("fail", f"{type(e).__name__}: {e}")


def do_ws(host, hold, rst, timeout):
    if create_connection is None:
        return ("fail", "no websocket-client")
    try:
        ws = create_connection(f"ws://{host}/snapshot", timeout=timeout)
        if hold > 0:
            time.sleep(hold)
        if rst:
            try:
                ws.sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, RST_LINGER)
                ws.sock.close()
            except OSError:
                pass
        else:
            ws.close()
        return ("ok", "")
    except (TimeoutError, socket.timeout):
        return ("hang", f"upgrade>{timeout}s")
    except Exception as e:  # noqa: BLE001 - 503 busy, refused, reset, etc. are all signal
        return ("fail", f"{type(e).__name__}: {e}")


def worker(stop, args, counters, modes):
    period = 1.0 / args.rate if args.rate > 0 else 0
    i = 0
    while not stop["flag"]:
        t0 = time.monotonic()
        mode = modes[i % len(modes)]
        i += 1
        if mode == "tcp":
            result, detail = do_tcp(args.host, args.port, args.hold, args.rst, args.timeout)
        elif mode == "http":
            result, detail = do_http(args.host, args.port, args.hold, args.rst, args.timeout)
        else:
            result, detail = do_ws(args.host, args.hold, args.rst, args.timeout)
        counters.add(mode, result, detail)
        if result != "ok":
            print(f"[{ts()}] {mode.upper()} {result.upper()}: {detail}")
        if period:
            dt = time.monotonic() - t0
            if dt < period:
                time.sleep(period - dt)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("host", nargs="?", default="hds.local")
    p.add_argument("--port", type=int, default=80)
    p.add_argument("--tcp", action="store_true", help="churn raw TCP connects")
    p.add_argument("--http", action="store_true", help="churn HTTP GET /")
    p.add_argument("--ws", action="store_true", help="churn second /snapshot WS clients")
    p.add_argument("--rst", action="store_true", help="abrupt RST closes (half-open server state)")
    p.add_argument("--rate", type=float, default=5.0, help="actions/sec per worker (default 5)")
    p.add_argument("--workers", type=int, default=2, help="concurrent churn workers (default 2)")
    p.add_argument("--hold", type=float, default=0.0, help="seconds to hold each connection open")
    p.add_argument("--timeout", type=float, default=4.0, help="per-connect timeout (a hit = server hang)")
    p.add_argument("--duration", type=float, default=180, help="seconds to run (0 = until Ctrl-C)")
    args = p.parse_args()

    modes = [m for m, on in (("tcp", args.tcp), ("http", args.http), ("ws", args.ws)) if on]
    if not modes:
        modes = ["tcp"]
        args.tcp = True

    # Resolve .local once and churn against the IP. Repeated mDNS lookups under
    # load fail intermittently (gaierror) and would be miscounted as server
    # failures; a real client caches the IP too.
    try:
        args.host = socket.gethostbyname(args.host)
    except OSError as e:
        sys.exit(f"could not resolve {args.host}: {e}")

    counters = Counters()
    stop = {"flag": False}
    threads = [threading.Thread(target=worker, args=(stop, args, counters, modes), daemon=True)
               for _ in range(args.workers)]

    print(f"[{ts()}] churn start: host={args.host} modes={modes} rst={args.rst} "
          f"rate={args.rate}/s workers={args.workers} hold={args.hold}s "
          f"duration={'inf' if not args.duration else args.duration}")
    for t in threads:
        t.start()

    started = time.monotonic()
    try:
        while True:
            if args.duration and (time.monotonic() - started) >= args.duration:
                break
            time.sleep(5)
            with counters.lock:
                print(f"[{ts()}] churn: ok={counters.ok} fail={counters.fail} "
                      f"hang={counters.hang} by_mode={counters.by_mode} "
                      f"last={counters.last_fail!r}")
    except KeyboardInterrupt:
        pass
    finally:
        stop["flag"] = True
        for t in threads:
            t.join(timeout=2)
        elapsed = time.monotonic() - started
        print(f"\n[{ts()}] CHURN SUMMARY ({elapsed:.0f}s): "
              f"ok={counters.ok} fail={counters.fail} hang={counters.hang} "
              f"by_mode={counters.by_mode}")
        rate = (counters.ok + counters.fail + counters.hang) / max(elapsed, 1)
        print(f"  attempted ~{rate:.1f} conn/s; hangs (connect timeouts) are the server-stall signal")


if __name__ == "__main__":
    main()
