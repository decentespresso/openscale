#!/usr/bin/env python3
"""
Decent-Scale BLE client for the Half Decent Scale, using bleak.

Two jobs:
  1. Provide a realistic "BT active" coexistence load while a WiFi 10 Hz stream
     runs (the goal requires both at once).
  2. Serve as the BLE regression test: it holds a persistent link, subscribes to
     weight notifications, and sends the keep-alive heartbeat the firmware
     requires. It logs every BLE disconnect and the notify rate -- 0 unexpected
     disconnects + steady notifies over a long run == BT is still reliable.

Protocol (from include/ble.h, include/config.h):
  service  0000fff0-0000-1000-8000-00805f9b34fb
  notify   0000fff4-0000-1000-8000-00805f9b34fb   (weight frames)
  write    000036f5-0000-1000-8000-00805f9b34fb   (commands)
  heartbeat command: 03 0A 03 FF FF 00 0A  (resets the scale's 5 s heartbeat
  timer; without it the scale drops BLE on purpose)

Requires: pip install bleak

Usage:
    python3 tools/ble_scale_client.py --duration 1500
    python3 tools/ble_scale_client.py --name "Decent Scale" --hb 2.0
"""

import argparse
import asyncio
import time
from datetime import datetime

from bleak import BleakClient, BleakScanner

SVC = "0000fff0-0000-1000-8000-00805f9b34fb"
NOTIFY = "0000fff4-0000-1000-8000-00805f9b34fb"
WRITE = "000036f5-0000-1000-8000-00805f9b34fb"
HEARTBEAT = bytes([0x03, 0x0A, 0x03, 0xFF, 0xFF, 0x00, 0x0A])


def ts():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


class Stats:
    def __init__(self):
        self.connects = 0
        self.disconnects = 0
        self.notifies = 0
        self.last_notify = 0.0
        self.max_gap = 0.0
        self.connect_latencies = []
        self.session_start = 0.0


async def run(args):
    st = Stats()
    started = time.monotonic()
    deadline = started + args.duration if args.duration else None

    while deadline is None or time.monotonic() < deadline:
        # Find the scale.
        t0 = time.monotonic()
        dev = await BleakScanner.find_device_by_name(args.name, timeout=args.scan_timeout)
        if dev is None:
            print(f"[{ts()}] scale '{args.name}' not found (scan {args.scan_timeout}s), retrying")
            continue
        try:
            disconnected = asyncio.Event()

            def on_disc(_c):
                st.disconnects += 1
                print(f"[{ts()}] *** BLE DISCONNECTED (#{st.disconnects}) after "
                      f"{time.monotonic()-st.session_start:.1f}s, {st.notifies} notifies")
                disconnected.set()

            async with BleakClient(dev, disconnected_callback=on_disc) as client:
                st.connects += 1
                st.connect_latencies.append(time.monotonic() - t0)
                st.session_start = time.monotonic()
                st.last_notify = time.monotonic()
                print(f"[{ts()}] BLE CONNECTED to {dev.address} "
                      f"in {(time.monotonic()-t0)*1000:.0f} ms (session #{st.connects})")

                def on_notify(_char, data: bytearray):
                    now = time.monotonic()
                    gap = now - st.last_notify
                    if gap > st.max_gap:
                        st.max_gap = gap
                    if gap > args.stall_threshold and st.notifies > 0:
                        print(f"[{ts()}] BLE notify stall: {gap:.1f}s since last frame")
                    st.last_notify = now
                    st.notifies += 1

                await client.start_notify(NOTIFY, on_notify)
                # Send an initial heartbeat immediately so the 5 s timer is fresh.
                await client.write_gatt_char(WRITE, HEARTBEAT, response=False)

                last_stat = time.monotonic()
                while not disconnected.is_set():
                    if deadline is not None and time.monotonic() >= deadline:
                        break
                    await asyncio.sleep(args.hb)
                    try:
                        await client.write_gatt_char(WRITE, HEARTBEAT, response=False)
                    except Exception as e:  # noqa: BLE001
                        print(f"[{ts()}] heartbeat write failed: {type(e).__name__}: {e}")
                        break
                    if time.monotonic() - last_stat >= 15:
                        last_stat = time.monotonic()
                        held = time.monotonic() - st.session_start
                        rate = st.notifies / held if held else 0
                        print(f"[{ts()}] BLE up {held:.0f}s notifies={st.notifies} "
                              f"(~{rate:.1f}/s) maxgap={st.max_gap:.2f}s")
                if deadline is not None and time.monotonic() >= deadline:
                    break
        except Exception as e:  # noqa: BLE001
            print(f"[{ts()}] BLE session error: {type(e).__name__}: {e}")
            await asyncio.sleep(2)

    elapsed = time.monotonic() - started
    print("\n" + "=" * 64)
    print(f"BLE SUMMARY  ran {elapsed:.0f}s  connects={st.connects} "
          f"unexpected_disconnects={st.disconnects} notifies={st.notifies}")
    if st.connect_latencies:
        cl = st.connect_latencies
        print(f"  connect latency: min {min(cl)*1000:.0f} mean {sum(cl)/len(cl)*1000:.0f} "
              f"max {max(cl)*1000:.0f} ms; max notify gap {st.max_gap:.2f}s")
    print(f"  VERDICT: {'BT OK (no unexpected drops)' if st.disconnects == 0 else 'BT DROPS SEEN'}")
    print("=" * 64)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--name", default="Decent Scale", help="BLE advertised name")
    p.add_argument("--duration", type=float, default=0, help="seconds (0 = until Ctrl-C)")
    p.add_argument("--hb", type=float, default=2.0, help="heartbeat interval s (scale drops at 5 s)")
    p.add_argument("--scan-timeout", type=float, default=10.0)
    p.add_argument("--stall-threshold", type=float, default=2.0)
    args = p.parse_args()
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
