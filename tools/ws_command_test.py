#!/usr/bin/env python3
import json, sys, time, websocket

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
START_REAL_UPDATE = "--start-update" in sys.argv
HOST = ARGS[0] if ARGS else "hds.local"
URL = f"ws://{HOST}/snapshot"
results = []

def connect():
    ws = websocket.create_connection(URL, timeout=6)
    ws.settimeout(0.4)
    return ws

def is_weight(d):
    return isinstance(d, dict) and "grams" in d and "type" not in d

def drain(ws, secs=0.15):
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
    send_capture(ws, "rate 10k", expect=("rate",))

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

    print("DISPLAY")
    send_capture(ws, '{"command":"display","action":"off"}', expect=("status",)); time.sleep(0.4)
    st = get_status(ws)
    record("display off", st.get("display_on") is False, f"display_on={st.get('display_on')}")
    send_capture(ws, '{"command":"display","action":"on"}', expect=("status",)); time.sleep(0.4)
    st = get_status(ws)
    record("display on", st.get("display_on") is True, f"display_on={st.get('display_on')}")

    print("LOW_POWER")
    send_capture(ws, '{"command":"low_power","action":"on"}', expect=("status",)); time.sleep(0.4)
    st = get_status(ws)
    record("low_power on", st.get("low_power") is True, f"low_power={st.get('low_power')}")
    send_capture(ws, '{"command":"low_power","action":"off"}', expect=("status",)); time.sleep(0.4)
    st = get_status(ws)
    record("low_power off", st.get("low_power") is False, f"low_power={st.get('low_power')}")

    print("SLEEP")
    send_capture(ws, '{"command":"sleep","action":"on"}', expect=("status",)); time.sleep(0.4)
    st = get_status(ws)
    record("sleep on", st.get("soft_sleep") is True, f"soft_sleep={st.get('soft_sleep')}")
    send_capture(ws, '{"command":"sleep","action":"wake"}', expect=("status",)); time.sleep(0.4)
    st = get_status(ws)
    record("sleep wake", st.get("soft_sleep") is False, f"soft_sleep={st.get('soft_sleep')}")

    print("WIFI UPDATE")
    resp = send_capture(ws, "wifi_update 9.9", expect=("error", "status"))
    record("wifi_update bad version -> error",
           bool(resp) and resp.get("type") == "error"
           and resp.get("code") == "ota_version_invalid",
           f"resp={resp}")
    resp = send_capture(ws, '{"command":"wifi_update","action":"3.1.15-rc1"}', expect=("error", "status"))
    record("wifi_update suffixed version -> error",
           bool(resp) and resp.get("type") == "error"
           and resp.get("code") == "ota_version_invalid",
           f"resp={resp}")

    if START_REAL_UPDATE:
        print("WIFI UPDATE (live install - this reflashes the scale)")
        resp = send_capture(ws, "wifi_update", expect=("status",))
        record("wifi_update bare -> accepted",
               bool(resp) and resp.get("type") == "status", f"resp={resp}")
        ws.close()
        print("update started; the scale reboots and the socket closes")
    else:
        print("skipping live wifi_update start; pass --start-update to run it")

        print("ERROR HANDLING")
        resp = send_capture(ws, '{"command":"zzz"}', expect=("error", "status"))
        record("bogus command -> error", bool(resp) and resp.get("type") == "error", f"resp={resp}")

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
