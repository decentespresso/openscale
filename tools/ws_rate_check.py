#!/usr/bin/env python3
"""
Verify the /snapshot stream is actually DELIVERED at the requested rate, not just
"connected with no drops". Measures three things over a window:

  * effective Hz  = weight frames received / elapsed
  * client inter-arrival jitter (wall-clock gaps between frames)
  * scale-side emission interval = delta of each frame's "ms" field (the scale's
    own millis() timestamp) -> the authoritative "is the firmware emitting at
    10 Hz", independent of network jitter.

A healthy 10 Hz stream: ~10 Hz effective, ms-deltas clustered at ~100 ms, few
gaps >150 ms.

Usage: python3 tools/ws_rate_check.py 192.168.10.242 --rate 10k --seconds 30
"""

import argparse
import json
import time
from datetime import datetime

from websocket import create_connection


def stats(xs):
    if not xs:
        return "n/a"
    s = sorted(xs)
    p = lambda q: s[min(len(s) - 1, int(q * len(s)))]
    return f"min={min(s):.0f} p50={p(.5):.0f} p95={p(.95):.0f} p99={p(.99):.0f} max={max(s):.0f}ms"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host", nargs="?", default="192.168.10.242")
    ap.add_argument("--rate", default="10k")
    ap.add_argument("--seconds", type=float, default=30)
    args = ap.parse_args()

    ws = create_connection(f"ws://{args.host}/snapshot", timeout=8)
    ws.send(f"rate {args.rate}")
    # Brief, TIME-BOUNDED drain of the rate ack/backlog. (Don't loop "until
    # timeout": at 10 Hz frames arrive every ~100 ms, so that never exits.)
    ws.settimeout(0.3)
    drain_end = time.monotonic() + 0.5
    while time.monotonic() < drain_end:
        try:
            ws.recv()
        except Exception:  # noqa: BLE001
            break

    arrivals = []
    ms_vals = []
    frames = 0
    end = time.monotonic() + args.seconds
    ws.settimeout(3.0)
    print(f"[{datetime.now():%H:%M:%S}] measuring {args.rate} for {args.seconds:.0f}s ...")
    while time.monotonic() < end:
        try:
            msg = ws.recv()
        except Exception as e:  # noqa: BLE001
            print(f"recv error after {frames} frames: {type(e).__name__}: {e}")
            break
        now = time.monotonic()
        try:
            o = json.loads(msg)
        except (ValueError, TypeError):
            continue
        if "grams" in o and "type" not in o:
            frames += 1
            arrivals.append(now)
            if "ms" in o:
                ms_vals.append(o["ms"])
    try:
        ws.close()
    except Exception:  # noqa: BLE001
        pass

    dur = (arrivals[-1] - arrivals[0]) if len(arrivals) > 1 else 0
    hz = (frames - 1) / dur if dur > 0 else 0
    gaps = [(arrivals[i + 1] - arrivals[i]) * 1000 for i in range(len(arrivals) - 1)]
    ms_d = [ms_vals[i + 1] - ms_vals[i] for i in range(len(ms_vals) - 1)]
    big = sum(1 for g in gaps if g > 150)
    miss = sum(1 for d in ms_d if d > 150)
    print(f"frames={frames} over {dur:.1f}s  ->  EFFECTIVE {hz:.2f} Hz")
    print(f"client inter-arrival: {stats(gaps)}   (gaps>150ms: {big})")
    print(f"scale emission (ms-delta): {stats(ms_d)}")
    print(f"scale frames spaced >150ms: {miss}/{len(ms_d)} ({100*miss/max(len(ms_d),1):.1f}%)")
    verdict = "GOOD ~10Hz" if 9.0 <= hz <= 11.0 and big <= max(1, len(gaps)//50) else "DEGRADED"
    print(f"VERDICT: {verdict}")


if __name__ == "__main__":
    main()
