#!/usr/bin/env python3
"""
WebSocket connection-drop reproducer for the Half Decent Scale firmware.

Mimics the Decenza client (rate 10k + events on + status, then consume the
~10 Hz JSON stream) and instruments the exact things the field bug report asks
about:

  * Does the connection drop on a ~1-2 min cadence?  -> inter-drop intervals
  * Is it a clean WS close (1000/1001) or abnormal (1006, no close frame)?
  * Does the stream stall right before the drop?      -> frame-gap timeline
  * How fast does reconnect to the same IP succeed?    -> reconnect latency

It auto-reconnects after each drop and prints a summary on exit (Ctrl-C or when
--duration elapses). Read-only against the scale: it never sends `power off`,
`sleep`, or any state-changing command beyond rate/events selection.

Requirements:
    pip install websocket-client

Usage:
    python3 tools/ws_drop_repro.py                      # ws://hds.local/snapshot, 10 Hz, runs until Ctrl-C
    python3 tools/ws_drop_repro.py 192.168.10.242
    python3 tools/ws_drop_repro.py --rate 10k --duration 300
    python3 tools/ws_drop_repro.py --no-events          # weight stream only, skip status subscription
"""

import argparse
import os
import signal
import sys
import time
from datetime import datetime

import socket as _socket

try:
    from websocket import (
        ABNF,
        WebSocketConnectionClosedException,
        WebSocketPayloadException,
        WebSocketTimeoutException,
        create_connection,
    )
except ImportError:
    sys.exit("Missing dependency. Install with: pip install websocket-client")


def now_wall():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def fmt_dur(seconds):
    if seconds is None:
        return "n/a"
    return f"{seconds:.1f}s"


class Stats:
    def __init__(self):
        self.connections = 0
        self.drops = []            # list of dicts, one per drop
        self.reconnect_latencies = []
        self.last_drop_monotonic = None

    def record_connect(self, latency):
        self.connections += 1
        if latency is not None:
            self.reconnect_latencies.append(latency)

    def record_drop(self, *, duration, frames, gap_before, kind, code, reason):
        interval = None
        t = time.monotonic()
        if self.last_drop_monotonic is not None:
            interval = t - self.last_drop_monotonic
        self.last_drop_monotonic = t
        self.drops.append(
            dict(duration=duration, frames=frames, gap_before=gap_before,
                 kind=kind, code=code, reason=reason, interval=interval)
        )
        return interval


def probe_socket(ws):
    """After a read failure, peek the raw TCP socket to learn HOW it ended:
    empty read == graceful FIN (EOF); RST == abrupt reset; timeout == half-open
    (peer vanished, e.g. WiFi dropped, no FIN/RST ever arrived)."""
    sock = getattr(ws, "sock", None)
    if sock is None:
        return ("no_socket", None, "socket already gone")
    try:
        sock.setblocking(True)
        sock.settimeout(3.0)
        rest = sock.recv(4096)
    except (ConnectionResetError, BrokenPipeError):
        return ("tcp_reset", 1006, "TCP RST (abrupt)")
    except (TimeoutError, _socket.timeout):
        return ("half_open", 1006, "no FIN/RST within 3s -- peer vanished (WiFi drop?)")
    except OSError as e:
        return ("tcp_error", None, f"{type(e).__name__}: {e}")
    if rest == b"":
        return ("tcp_fin", 1006, "graceful FIN / EOF, no WS close frame")
    # Some trailing bytes -- check for a trailing WS CLOSE frame (opcode 0x8).
    if len(rest) >= 1 and (rest[0] & 0x0F) == ABNF.OPCODE_CLOSE:
        body = rest[2:]
        code = int.from_bytes(body[:2], "big") if len(body) >= 2 else None
        return ("ws_close", code, "trailing WS close frame")
    return ("data_then_eof", None, f"{len(rest)} trailing bytes then close")


def classify_close(ws, exc):
    """Return (kind, code, reason) describing how the connection ended."""
    # websocket-client stashes a received CLOSE frame here, if any arrived.
    cf = getattr(ws, "close_frame", None)
    if cf is not None and getattr(cf, "data", None):
        data = cf.data
        code = int.from_bytes(data[:2], "big") if len(data) >= 2 else None
        reason = data[2:].decode("utf-8", "replace") if len(data) > 2 else ""
        return ("ws_close", code, reason)
    # PayloadException = the stream broke mid-frame; the underlying socket state
    # (FIN vs RST vs half-open) is the real signal, so probe it.
    if isinstance(exc, (WebSocketConnectionClosedException, WebSocketPayloadException)):
        return probe_socket(ws)
    if isinstance(exc, (ConnectionResetError,)):
        return ("tcp_reset", 1006, "TCP RST")
    if isinstance(exc, OSError):
        return ("tcp_error", None, f"{type(exc).__name__}: {exc}")
    return probe_socket(ws)


def run_session(host, url, args, stats, deadline):
    """One connect->stream->drop cycle. Returns False if connect failed."""
    t0 = time.monotonic()
    try:
        ws = create_connection(url, timeout=args.connect_timeout)
    except Exception as e:  # noqa: BLE001 - report any connect failure verbatim
        print(f"[{now_wall()}] CONNECT FAILED ({fmt_dur(time.monotonic() - t0)}): {e}")
        return False
    connect_latency = time.monotonic() - t0
    stats.record_connect(connect_latency if stats.connections > 0 else None)
    print(f"[{now_wall()}] CONNECTED in {connect_latency * 1000:.0f} ms  (session #{stats.connections})")

    # Mimic the Decenza handshake.
    handshake = []
    if args.rate:
        handshake.append(f"rate {args.rate}")
    if args.events:
        handshake.append("events on")
        handshake.append("status")
    for cmd in handshake:
        try:
            ws.send(cmd)
        except Exception as e:  # noqa: BLE001
            print(f"[{now_wall()}] send({cmd!r}) failed: {e}")

    conn_start = time.monotonic()
    last_frame = conn_start
    frames = 0
    max_gap = 0.0
    stalled_since = None

    while True:
        if deadline and time.monotonic() > deadline:
            # Hit the overall test budget mid-session; close cleanly and stop.
            ws.close()
            print(f"[{now_wall()}] duration budget reached, closing test")
            return "done"
        try:
            ws.settimeout(args.recv_timeout)
            opcode, data = ws.recv_data(control_frame=True)
        except WebSocketTimeoutException:
            # No frame within recv_timeout. At 10 Hz this is already a stall.
            gap = time.monotonic() - last_frame
            if gap > args.stall_threshold and stalled_since is None:
                stalled_since = last_frame
                print(f"[{now_wall()}] STALL: no frame for {gap:.1f}s "
                      f"(stream was {args.rate or '?'}; expected ~{1/_hz(args):.2f}s gaps)")
            continue
        except Exception as e:  # noqa: BLE001 - any of several disconnect exceptions
            gap = time.monotonic() - last_frame
            duration = time.monotonic() - conn_start
            kind, code, reason = classify_close(ws, e)
            interval = stats.record_drop(duration=duration, frames=frames,
                                         gap_before=gap, kind=kind, code=code, reason=reason)
            print(f"[{now_wall()}] *** DROP after {fmt_dur(duration)} "
                  f"({frames} frames) | last frame {gap:.1f}s ago | "
                  f"{kind} code={code} {reason!r}"
                  + (f" | {fmt_dur(interval)} since previous drop" if interval else ""))
            try:
                ws.close()
            except Exception:  # noqa: BLE001
                pass
            return True

        t = time.monotonic()
        if opcode == ABNF.OPCODE_CLOSE:
            code = int.from_bytes(data[:2], "big") if data and len(data) >= 2 else None
            reason = data[2:].decode("utf-8", "replace") if data and len(data) > 2 else ""
            duration = t - conn_start
            gap = t - last_frame
            interval = stats.record_drop(duration=duration, frames=frames, gap_before=gap,
                                         kind="ws_close", code=code, reason=reason)
            print(f"[{now_wall()}] *** WS CLOSE FRAME after {fmt_dur(duration)} "
                  f"({frames} frames) | code={code} reason={reason!r}"
                  + (f" | {fmt_dur(interval)} since previous drop" if interval else ""))
            try:
                ws.close()
            except Exception:  # noqa: BLE001
                pass
            return True

        if opcode in (ABNF.OPCODE_TEXT, ABNF.OPCODE_BINARY):
            gap = t - last_frame
            if gap > max_gap:
                max_gap = gap
            if stalled_since is not None:
                print(f"[{now_wall()}] stream resumed after {gap:.1f}s gap")
                stalled_since = None
            last_frame = t
            frames += 1
            if args.verbose and frames % args.print_every == 0:
                print(f"[{now_wall()}] ... {frames} frames, max gap so far {max_gap * 1000:.0f} ms")


def _hz(args):
    label = (args.rate or "10").lower().rstrip("khz")
    try:
        return float(label) or 10.0
    except ValueError:
        return 10.0


def print_summary(stats, started):
    elapsed = time.monotonic() - started
    print("\n" + "=" * 70)
    print(f"SUMMARY  (ran {fmt_dur(elapsed)}, {stats.connections} connection(s), "
          f"{len(stats.drops)} drop(s))")
    print("=" * 70)
    if not stats.drops:
        print("No drops observed.")
    for i, d in enumerate(stats.drops, 1):
        print(f"  drop #{i}: held {fmt_dur(d['duration'])}, {d['frames']} frames, "
              f"gap-before {d['gap_before']:.1f}s, {d['kind']} code={d['code']}"
              + (f", {fmt_dur(d['interval'])} after prev" if d['interval'] else ""))
    if stats.drops:
        intervals = [d["interval"] for d in stats.drops if d["interval"] is not None]
        durations = [d["duration"] for d in stats.drops]
        kinds = {}
        for d in stats.drops:
            kinds[d["kind"]] = kinds.get(d["kind"], 0) + 1
        print("-" * 70)
        print(f"  session duration: min {min(durations):.1f}s  "
              f"mean {sum(durations)/len(durations):.1f}s  max {max(durations):.1f}s")
        if intervals:
            print(f"  inter-drop interval: min {min(intervals):.1f}s  "
                  f"mean {sum(intervals)/len(intervals):.1f}s  max {max(intervals):.1f}s")
        print(f"  close kinds: {kinds}")
        if stats.reconnect_latencies:
            r = stats.reconnect_latencies
            print(f"  reconnect latency: min {min(r)*1000:.0f} ms  "
                  f"mean {sum(r)/len(r)*1000:.0f} ms  max {max(r)*1000:.0f} ms")
    print("=" * 70)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("host", nargs="?", default=os.environ.get("HDS_HOST", "hds.local"),
                   help="scale host or IP (default: hds.local)")
    p.add_argument("--rate", default="10k",
                   help="stream rate label sent to the scale: 2k/5k/10k (default 10k)")
    p.add_argument("--no-events", dest="events", action="store_false",
                   help="don't enable events/status (weight stream only)")
    p.add_argument("--duration", type=float, default=0,
                   help="total seconds to run, across reconnects (0 = until Ctrl-C)")
    p.add_argument("--reconnect-delay", type=float, default=5.0,
                   help="seconds to wait before reconnecting after a drop "
                        "(default 5, matching the reported client)")
    p.add_argument("--recv-timeout", type=float, default=1.0,
                   help="per-recv timeout; gaps beyond this are detected as stalls")
    p.add_argument("--stall-threshold", type=float, default=1.0,
                   help="seconds without a frame before logging a STALL (default 1.0)")
    p.add_argument("--connect-timeout", type=float, default=10.0)
    p.add_argument("-v", "--verbose", action="store_true", help="periodic frame counts")
    p.add_argument("--print-every", type=int, default=100)
    args = p.parse_args()

    url = f"ws://{args.host}/snapshot"
    stats = Stats()
    started = time.monotonic()

    stop = {"flag": False}

    def on_sigint(signum, frame):  # noqa: ARG001
        stop["flag"] = True
        print(f"\n[{now_wall()}] interrupted, finishing up...")

    signal.signal(signal.SIGINT, on_sigint)

    print(f"Target: {url}  rate={args.rate} events={args.events} "
          f"reconnect_delay={args.reconnect_delay}s "
          f"duration={'unbounded' if not args.duration else str(args.duration)+'s'}")
    print(f"Start: {now_wall()}\n")

    deadline = (started + args.duration) if args.duration else 0
    try:
        while not stop["flag"]:
            if deadline and time.monotonic() > deadline:
                break
            result = run_session(args.host, url, args, stats, deadline)
            if result == "done":
                break
            if stop["flag"]:
                break
            if result is False:
                # connect failed; brief backoff then retry
                time.sleep(min(args.reconnect_delay, 3.0))
                continue
            # mimic the client's reconnect delay
            for _ in range(int(args.reconnect_delay * 10)):
                if stop["flag"]:
                    break
                time.sleep(0.1)
    finally:
        print_summary(stats, started)


if __name__ == "__main__":
    main()
