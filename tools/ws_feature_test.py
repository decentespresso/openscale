#!/usr/bin/env python3
"""
WebSocket feature test for the Half Decent Scale firmware.

Exercises the on-device /snapshot WebSocket control + event protocol against a
running scale, as a quick regression check after firmware changes. WiFi must be
enabled on the device (it advertises _decentscale._tcp / hds.local).

Requirements:
    pip install websocket-client

Usage:
    python3 tools/ws_feature_test.py                # ws://hds.local/snapshot
    python3 tools/ws_feature_test.py 192.168.1.50   # or pass a host / IP
    HDS_HOST=192.168.1.50 python3 tools/ws_feature_test.py

Exits 0 if every check passes, 1 otherwise. Never sends `power off` (it cannot
be woken over the network) and restores display/sleep/timer state when done.
"""

import argparse
import json
import os
import sys
import time

try:
    from websocket import create_connection, WebSocketTimeoutException
except ImportError:
    sys.exit("Missing dependency. Install with: pip install websocket-client")

results = []


def rec(name, ok, detail=""):
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f" -- {detail}" if detail else ""))


def is_weight(o):
    # Weight snapshots are intentionally untyped for backwards compatibility.
    return "grams" in o and "type" not in o


def is_type(t):
    return lambda o: o.get("type") == t


def recv_until(ws, pred, timeout=4.0):
    """Read frames until one satisfies pred (skipping others), or timeout."""
    end = time.time() + timeout
    while time.time() < end:
        try:
            ws.settimeout(max(0.1, end - time.time()))
            msg = ws.recv()
        except (WebSocketTimeoutException, OSError):
            break
        try:
            obj = json.loads(msg)
        except (ValueError, TypeError):
            continue
        if pred(obj):
            return obj
    return None


def drain(ws, dur=0.5):
    """Discard any buffered frames so the next read reflects current state."""
    end = time.time() + dur
    while time.time() < end:
        try:
            ws.settimeout(max(0.05, end - time.time()))
            ws.recv()
        except (WebSocketTimeoutException, OSError):
            break


def status_after(ws, command, settle=0.4):
    """Send a command, let it apply (synchronous flags + one main-loop tick for
    deferred ops), discard the backlog, then read a fresh status frame."""
    ws.send(command)
    time.sleep(settle)
    drain(ws)
    ws.send("status")
    return recv_until(ws, is_type("status"))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host", nargs="?",
                    default=os.environ.get("HDS_HOST", "hds.local"),
                    help="scale host or IP (default: hds.local)")
    url = f"ws://{ap.parse_args().host}/snapshot"

    try:
        ws = create_connection(url, timeout=6)
    except OSError as e:
        sys.exit(f"Could not connect to {url}: {e}\n"
                 "Is WiFi enabled on the scale and reachable? Try passing its IP.")
    print(f"Connected to {url}\n")

    rec("weight stream (untyped snapshot frames)", recv_until(ws, is_weight) is not None)

    ws.send("status")
    st = recv_until(ws, is_type("status")) or {}
    fields = ("battery_percent", "timer_running", "timer_seconds", "display_on",
              "low_power", "soft_sleep", "events_enabled", "rate_hz", "interval_ms",
              "wifi_on_boot", "wifi_active", "wifi_connected", "wifi_mode",
              "wifi_credentials_saved", "ble_enabled", "ble_connected",
              "ble_buttons_enabled", "ble_heartbeat_required",
              "auto_sleep_enabled", "auto_sleep_minutes",
              "quick_boot_enabled", "button_boot_delay_ms", "screen_flipped",
              "time_on_top", "drift_compensation_max_grams",
              "buzzer_enabled", "mode", "mode_name",
              "pourover_target_grams", "espresso_target_grams",
              "calibration_factor", "battery_calibration_factor")
    rec("status frame: all fields present, 'led' removed",
        all(k in st for k in fields) and "led" not in st,
        f"keys={sorted(st)}")

    rec("events on", (status_after(ws, "events on") or {}).get("events_enabled") is True)

    ws.send("rate 10k")
    r = recv_until(ws, is_type("rate")) or {}
    rec("rate 10k -> 100 ms / 10 Hz", r.get("interval_ms") == 100 and r.get("hz") == 10)

    # New behavior: unrecognized / malformed input gets an error frame, not silence.
    ws.send("not_a_real_command")
    e = recv_until(ws, is_type("error")) or {}
    rec("error frame on unknown command", e.get("code") == "unknown_command")
    ws.send("{ not valid json")
    e = recv_until(ws, is_type("error")) or {}
    rec("error frame on malformed JSON", e.get("code") == "unknown_command")

    # Timer ops are deferred to the main loop; the effect lands within a tick.
    ws.send("timer start")
    time.sleep(1.6)
    st = status_after(ws, "status") or {}
    rec("timer start (deferred) -> running, >=1 s",
        st.get("timer_running") is True and st.get("timer_seconds", 0) >= 1)
    rec("timer stop -> not running",
        (status_after(ws, "timer stop") or {}).get("timer_running") is False)
    rec("timer zero -> 0 s",
        (status_after(ws, "timer zero") or {}).get("timer_seconds") == 0)

    rec("display off -> display_on:false",
        (status_after(ws, "display off") or {}).get("display_on") is False)
    rec("display on -> display_on:true",
        (status_after(ws, "display on") or {}).get("display_on") is True)

    rec("low_power on -> low_power:true",
        (status_after(ws, "low_power on") or {}).get("low_power") is True)
    status_after(ws, "low_power off")  # restore

    st = status_after(ws, "soft_sleep on") or {}
    rec("soft_sleep on -> soft_sleep:true and display_on:false",
        st.get("soft_sleep") is True and st.get("display_on") is False)
    st = status_after(ws, "soft_sleep off") or {}
    rec("soft_sleep off -> restored",
        st.get("soft_sleep") is False and st.get("display_on") is True)
    ws.close()

    # Multi-client: independent serving, and one client leaving must not stall
    # the other.
    a = create_connection(url, timeout=6)
    b = create_connection(url, timeout=6)
    rec("multi-client: both clients receive weight",
        recv_until(a, is_weight) is not None and recv_until(b, is_weight) is not None)
    a.close()
    time.sleep(0.6)
    rec("multi-client: survivor still streams after peer disconnects",
        recv_until(b, is_weight) is not None)
    b.close()

    passed, total = sum(results), len(results)
    print(f"\n{passed}/{total} checks passed")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
