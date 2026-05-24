#!/usr/bin/env python3
"""Exercise every WebSocket control command of the Half Decent Scale and report
PASS/FAIL by checking the server's response frame and the resulting device state
(read back from the status frame). Non-destructive: it restores display/sleep/
low-power/events at the end and never sends `power off`.

Usage: python3 tools/ws_command_test.py [host]   (default hds.local)

Robustness notes (the scale streams weight at up to 10 Hz, so command responses
interleave with weight frames and can lag):
  * drain() clears any queued frames before each command, so a lagged response
    from the *previous* command isn't mis-attributed to the next one.
  * send_capture() waits for a frame of the expected `type` and returns the LAST
    such frame in the window -- a stray lagged frame is superseded by the real
    response (which is sent most recently).
  * events on/off are verified from the command's OWN status response, never via
    a separate status fetch (fetching status by re-sending `events on` would
    itself flip events_enabled back to true).
"""
import json, sys, time, websocket

HOST = sys.argv[1] if len(sys.argv) > 1 else "hds.local"
URL = f"ws://{HOST}/snapshot"
results = []

def connect():
    ws = websocket.create_connection(URL, timeout=6)
    ws.settimeout(0.4)
    return ws

def is_weight(d):
    return isinstance(d, dict) and "grams" in d and "type" not in d

def drain(ws, secs=0.15):
    """Consume already-queued frames so a lagged prior response doesn't pollute
    the next command's capture."""
    old = ws.gettimeout()
    ws.settimeout(0.05)
    end = time.time() + secs
    try:
        while time.time() < end:
            try:
                ws.recv()
            except Exception:
                pass
    finally:
        ws.settimeout(old)

def send_capture(ws, msg, expect=None, window=1.5):
    """Send msg; return the LAST non-weight frame matching `expect` (a tuple of
    type strings, or None for any) within the window, else None."""
    drain(ws)
    ws.send(msg)
    end = time.time() + window
    last = None
    while time.time() < end:
        try:
            m = ws.recv()
        except Exception:
            continue
        try:
            d = json.loads(m)
        except Exception:
            continue
        if is_weight(d):
            continue
        if expect is None or d.get("type") in expect:
            last = d
    return last

def get_status(ws):
    """Fetch a fresh full status frame. NOTE: this re-enables events as a side
    effect, so it must NOT be used to verify the events on/off commands."""
    st = send_capture(ws, '{"command":"events","action":"on"}', expect=("status",))
    return st or {}

def latest_grams(ws, secs=1.5):
    end = time.time() + secs
    g = None
    while time.time() < end:
        try:
            d = json.loads(ws.recv())
            if "grams" in d:
                g = d["grams"]
        except Exception:
            pass
    return g

def record(name, ok, detail):
    results.append((name, ok, detail))
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}: {detail}")

def main():
    ws = connect()
    print(f"connected {URL}\n")

    # ---- RATE ---- (response type "rate")
    print("RATE")
    for label, msg, want_hz in [
        ("bare rate 2k", "rate 2k", 2),
        ("bare rate 5k", "rate 5k", 5),
        ("bare rate 10k", "rate 10k", 10),
        ('json {"rate":"5k"}', '{"rate":"5k"}', 5),
        ('json command rate value 2k', '{"command":"rate","value":"2k"}', 2),
        ('json {"rate_hz":10}', '{"rate_hz":10}', 10),
        ('json {"hz":5}', '{"hz":5}', 5),
        ('json {"interval_ms":200}', '{"interval_ms":200}', 5),
        ("bare interval 100", "interval 100", 10),
    ]:
        resp = send_capture(ws, msg, expect=("rate",))
        hz = (resp or {}).get("hz")
        record(label, resp is not None and hz == want_hz, f"resp={resp}")
    resp = send_capture(ws, "get_rate", expect=("rate",))
    record("bare get_rate", bool(resp) and resp.get("type") == "rate", f"resp={resp}")
    send_capture(ws, "rate 10k", expect=("rate",))  # leave at 10Hz

    # ---- EVENTS ---- (verify from the command's OWN status response)
    print("EVENTS")
    resp = send_capture(ws, "events off", expect=("status",))
    record("bare events off", resp is not None and resp.get("events_enabled") is False,
           f"events_enabled={(resp or {}).get('events_enabled')}")
    resp = send_capture(ws, "events on", expect=("status",))
    record("bare events on", resp is not None and resp.get("events_enabled") is True,
           f"events_enabled={(resp or {}).get('events_enabled')}")
    resp = send_capture(ws, '{"command":"events","action":"on"}', expect=("status",))
    record('json command events on', bool(resp) and resp.get("status") == "ok",
           f"resp_status={(resp or {}).get('status')}")
    resp = send_capture(ws, '{"events":"on"}', expect=("status", "error"))
    record('json {"events":"on"} shorthand',
           bool(resp) and resp.get("type") == "status" and resp.get("events_enabled") is True,
           f"resp_type={(resp or {}).get('type')} events_enabled={(resp or {}).get('events_enabled')}")

    # ---- TARE ---- (weight must move toward ~0; send raw so we don't drain the baseline)
    print("TARE")
    def tare_test(label, cmd):
        before = latest_grams(ws, 1.2)
        ws.send(cmd)
        time.sleep(1.3)
        after = latest_grams(ws, 1.5)
        ok = (before is not None and after is not None
              and abs(after) < max(1.0, abs(before) * 0.3))
        record(label, ok, f"grams {before} -> {after}")
    tare_test('json command tare (weight -> ~0)', '{"command":"tare"}')
    tare_test('bare tare (weight -> ~0)', 'tare')

    # ---- TIMER ---- (deferred to main loop, so verify via a delayed status read)
    print("TIMER")
    send_capture(ws, '{"command":"timer","action":"start"}', expect=("status",)); time.sleep(0.6)
    st = get_status(ws)
    record("timer start", st.get("timer_running") is True, f"timer_running={st.get('timer_running')}")
    time.sleep(0.8)
    st2 = get_status(ws)
    record("timer counts up", (st2.get("timer_seconds") or 0) >= (st.get("timer_seconds") or 0),
           f"seconds {st.get('timer_seconds')} -> {st2.get('timer_seconds')}")
    send_capture(ws, '{"command":"timer","action":"stop"}', expect=("status",)); time.sleep(0.5)
    st = get_status(ws)
    record("timer stop", st.get("timer_running") is False, f"timer_running={st.get('timer_running')}")
    send_capture(ws, '{"command":"timer","action":"zero"}', expect=("status",)); time.sleep(0.5)
    st = get_status(ws)
    record("timer zero", (st.get("timer_seconds") or 0) == 0, f"timer_seconds={st.get('timer_seconds')}")

    # ---- DISPLAY ----
    print("DISPLAY")
    send_capture(ws, '{"command":"display","action":"off"}', expect=("status",)); time.sleep(0.4)
    st = get_status(ws)
    record("display off", st.get("display_on") is False, f"display_on={st.get('display_on')}")
    send_capture(ws, '{"command":"display","action":"on"}', expect=("status",)); time.sleep(0.4)
    st = get_status(ws)
    record("display on", st.get("display_on") is True, f"display_on={st.get('display_on')}")

    # ---- LOW POWER ----
    print("LOW_POWER")
    send_capture(ws, '{"command":"low_power","action":"on"}', expect=("status",)); time.sleep(0.4)
    st = get_status(ws)
    record("low_power on", st.get("low_power") is True, f"low_power={st.get('low_power')}")
    send_capture(ws, '{"command":"low_power","action":"off"}', expect=("status",)); time.sleep(0.4)
    st = get_status(ws)
    record("low_power off", st.get("low_power") is False, f"low_power={st.get('low_power')}")

    # ---- SOFT SLEEP ----
    print("SLEEP")
    send_capture(ws, '{"command":"sleep","action":"on"}', expect=("status",)); time.sleep(0.4)
    st = get_status(ws)
    record("sleep on", st.get("soft_sleep") is True, f"soft_sleep={st.get('soft_sleep')}")
    send_capture(ws, '{"command":"sleep","action":"wake"}', expect=("status",)); time.sleep(0.4)
    st = get_status(ws)
    record("sleep wake", st.get("soft_sleep") is False, f"soft_sleep={st.get('soft_sleep')}")

    # ---- BOGUS (should error) ----
    print("ERROR HANDLING")
    resp = send_capture(ws, '{"command":"zzz"}', expect=("error", "status"))
    record("bogus command -> error", bool(resp) and resp.get("type") == "error", f"resp={resp}")

    # restore safe state
    send_capture(ws, '{"command":"display","action":"on"}', expect=("status",))
    send_capture(ws, '{"command":"low_power","action":"off"}', expect=("status",))
    send_capture(ws, '{"command":"sleep","action":"wake"}', expect=("status",))
    send_capture(ws, "rate 10k", expect=("rate",))
    ws.close()

    npass = sum(1 for _, ok, _ in results if ok)
    print(f"\n==== {npass}/{len(results)} PASS ====")
    fails = [n for n, ok, _ in results if not ok]
    if fails:
        print("FAILURES:")
        for f in fails:
            print(f"  - {f}")

if __name__ == "__main__":
    main()
