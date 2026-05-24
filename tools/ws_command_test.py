#!/usr/bin/env python3
"""Exercise every WebSocket control command of the Half Decent Scale and report
PASS/FAIL by checking the server's response frame and the resulting device state
(read back from the status frame). Non-destructive: it restores display/sleep/
low-power/events at the end and never sends `power off`.

Usage: python3 tools/ws_command_test.py [host]   (default hds.local)
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

def send_capture(ws, msg, window=1.0):
    """Send msg; return (response_dict_or_None, raw_frames) collected over window.
    response = first non-weight frame (status/rate/error)."""
    ws.send(msg)
    end = time.time() + window
    resp = None
    raws = []
    while time.time() < end:
        try:
            m = ws.recv()
        except Exception:
            continue
        raws.append(m)
        try:
            d = json.loads(m)
        except Exception:
            continue
        if not is_weight(d) and resp is None:
            resp = d
    return resp, raws

def get_status(ws):
    """Fetch a fresh full status frame (events-on is idempotent and returns one)."""
    resp, _ = send_capture(ws, '{"command":"events","action":"on"}', 1.2)
    if resp and resp.get("type") == "status":
        return resp
    # fall back: wait for a periodic status
    end = time.time() + 6
    while time.time() < end:
        try:
            d = json.loads(ws.recv())
            if d.get("type") == "status":
                return d
        except Exception:
            pass
    return {}

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

    # ---- RATE ----
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
        resp, _ = send_capture(ws, msg)
        hz = (resp or {}).get("hz") or (resp or {}).get("rate_hz")
        record(label, resp is not None and hz == want_hz, f"resp={resp}")
    resp, _ = send_capture(ws, "get_rate")
    record("bare get_rate", bool(resp) and resp.get("type") == "rate", f"resp={resp}")
    send_capture(ws, "rate 10k")  # leave at 10Hz

    # ---- EVENTS ----
    print("EVENTS")
    resp, _ = send_capture(ws, "events off")
    st = get_status(ws)
    record("bare events off", st.get("events_enabled") is False, f"events_enabled={st.get('events_enabled')}")
    resp, _ = send_capture(ws, "events on")
    record("bare events on", bool(resp), f"resp_type={(resp or {}).get('type')}")
    resp, _ = send_capture(ws, '{"command":"events","action":"on"}')
    record('json command events on', bool(resp) and resp.get("status") == "ok",
           f"resp_status={(resp or {}).get('status')}")
    resp, _ = send_capture(ws, '{"events":"on"}')
    record('json {"events":"on"} shorthand', bool(resp) and resp.get("type") != "error",
           f"resp={resp}")

    # ---- TARE ----
    print("TARE")
    before = latest_grams(ws, 1.0)
    send_capture(ws, '{"command":"tare"}', 0.5)
    time.sleep(0.7)
    after = latest_grams(ws, 1.5)
    tared = (before is not None and after is not None and abs(after) < max(1.0, abs(before) * 0.3))
    record('json command tare (weight -> ~0)', tared, f"grams {before} -> {after}")
    before = latest_grams(ws, 1.0)
    send_capture(ws, "tare", 0.5)
    time.sleep(0.7)
    after = latest_grams(ws, 1.5)
    tared2 = (before is not None and after is not None and abs(after) < max(1.0, abs(before) * 0.3))
    record('bare tare (weight -> ~0)', tared2, f"grams {before} -> {after}")

    # ---- TIMER ----
    print("TIMER")
    send_capture(ws, '{"command":"timer","action":"start"}', 0.5); time.sleep(0.6)
    st = get_status(ws)
    record("timer start", st.get("timer_running") is True, f"timer_running={st.get('timer_running')}")
    time.sleep(0.8)
    st2 = get_status(ws)
    record("timer counts up", (st2.get("timer_seconds") or 0) >= (st.get("timer_seconds") or 0),
           f"seconds {st.get('timer_seconds')} -> {st2.get('timer_seconds')}")
    send_capture(ws, '{"command":"timer","action":"stop"}', 0.5); time.sleep(0.5)
    st = get_status(ws)
    record("timer stop", st.get("timer_running") is False, f"timer_running={st.get('timer_running')}")
    send_capture(ws, '{"command":"timer","action":"zero"}', 0.5); time.sleep(0.5)
    st = get_status(ws)
    record("timer zero", (st.get("timer_seconds") or 0) == 0, f"timer_seconds={st.get('timer_seconds')}")

    # ---- DISPLAY ----
    print("DISPLAY")
    send_capture(ws, '{"command":"display","action":"off"}', 0.5); time.sleep(0.4)
    st = get_status(ws)
    record("display off", st.get("display_on") is False, f"display_on={st.get('display_on')}")
    send_capture(ws, '{"command":"display","action":"on"}', 0.5); time.sleep(0.4)
    st = get_status(ws)
    record("display on", st.get("display_on") is True, f"display_on={st.get('display_on')}")

    # ---- LOW POWER ----
    print("LOW_POWER")
    send_capture(ws, '{"command":"low_power","action":"on"}', 0.5); time.sleep(0.4)
    st = get_status(ws)
    record("low_power on", st.get("low_power") is True, f"low_power={st.get('low_power')}")
    send_capture(ws, '{"command":"low_power","action":"off"}', 0.5); time.sleep(0.4)
    st = get_status(ws)
    record("low_power off", st.get("low_power") is False, f"low_power={st.get('low_power')}")

    # ---- SOFT SLEEP ----
    print("SLEEP")
    send_capture(ws, '{"command":"sleep","action":"on"}', 0.5); time.sleep(0.4)
    st = get_status(ws)
    record("sleep on", st.get("soft_sleep") is True, f"soft_sleep={st.get('soft_sleep')}")
    send_capture(ws, '{"command":"sleep","action":"wake"}', 0.5); time.sleep(0.4)
    st = get_status(ws)
    record("sleep wake", st.get("soft_sleep") is False, f"soft_sleep={st.get('soft_sleep')}")

    # ---- BOGUS (should error) ----
    print("ERROR HANDLING")
    resp, _ = send_capture(ws, '{"command":"zzz"}')
    record("bogus command -> error", bool(resp) and resp.get("type") == "error", f"resp={resp}")

    # restore safe state
    send_capture(ws, '{"command":"display","action":"on"}', 0.3)
    send_capture(ws, '{"command":"low_power","action":"off"}', 0.3)
    send_capture(ws, '{"command":"sleep","action":"wake"}', 0.3)
    send_capture(ws, "rate 10k", 0.3)
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
