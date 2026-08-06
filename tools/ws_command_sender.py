#!/usr/bin/env python3

import argparse
import time
from datetime import datetime

from websocket import create_connection, WebSocketException

COMMANDS = ["status", "events on", "rate 10k", "tare",
            "timer start", "timer stop", "timer zero", "status"]


def ts():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("host", nargs="?", default="192.168.10.242")
    p.add_argument("--interval", type=float, default=2.0)
    p.add_argument("--duration", type=float, default=0)
    args = p.parse_args()
    url = f"ws://{args.host}/snapshot"

    started = time.monotonic()
    sent = ok = errors = reconnects = 0
    ws = None
    i = 0
    print(f"[{ts()}] command sender -> {url} every {args.interval}s")
    while not args.duration or (time.monotonic() - started) < args.duration:
        try:
            if ws is None:
                ws = create_connection(url, timeout=8)
                reconnects += 1
                print(f"[{ts()}] connected (#{reconnects})")
            cmd = COMMANDS[i % len(COMMANDS)]
            i += 1
            ws.send(cmd)
            sent += 1
            ws.settimeout(1.0)
            try:
                while True:
                    ws.recv()
            except Exception:  # noqa: BLE001 - timeout / no more frames
                pass
            ok += 1
            if sent % 20 == 0:
                print(f"[{ts()}] sent={sent} ok={ok} errors={errors} reconnects={reconnects}", flush=True)
        except (WebSocketException, OSError) as e:
            errors += 1
            print(f"[{ts()}] cmd error: {type(e).__name__}: {e}", flush=True)
            try:
                ws.close()
            except Exception:  # noqa: BLE001
                pass
            ws = None
            time.sleep(2)
            continue
        time.sleep(args.interval)

    print(f"\n[{ts()}] CMD SUMMARY sent={sent} ok={ok} errors={errors} reconnects={reconnects}", flush=True)


if __name__ == "__main__":
    main()
