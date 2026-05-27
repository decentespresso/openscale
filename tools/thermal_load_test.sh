#!/bin/bash
# 1-hour multi-protocol thermal/stall load test.
#   - drives:   USB 10 Hz binary + WS 10 Hz stream + HTTP/WS churn + mDNS
#   - NOT driven here: BT (the user's app drives it concurrently)
#   - monitors: WS telemetry across three frame types (status / debug / session_info):
#                 status (periodic, 5 s)      grams, charging, timer, display state
#                 debug snapshot (on demand)  soc_temp_c/max, weight_stalled, stall_count,
#                                             last_stall_*, adc_recovery_count
#                 debug events                stall_start/stall_end/adc_recovery/temp_peak
#                 session_info (on connect)   firmware_version, reset_reason
#               Polls {"command":"debug"} every ~60 s to refresh the snapshot
#               fields, watching for the "weight stops being collected" failure
#               and the temp at which it hits.
# Opening USB reboots the scale once (clean baseline); reconnect BT afterward.
#
# Usage: tools/thermal_load_test.sh [DURATION_S] [IP] [HOST]
set -u
cd "$(dirname "$0")/.."
DUR="${1:-3600}"
IP="${2:-192.168.10.242}"
HOST="${3:-hds.local}"
PORT="$(ls /dev/cu.*usbserial* 2>/dev/null | head -1)"
LOG=/tmp/thermal; rm -rf "$LOG"; mkdir -p "$LOG"
ts(){ date +%H:%M:%S; }
echo "[thermal] START $(ts) dur=${DUR}s ip=$IP host=$HOST port=${PORT:-<none>}"
[ -z "$PORT" ] && echo "[thermal] WARNING: no USB serial port found -- USB 10Hz load DISABLED (WiFi-only)"

# 1) USB 10 Hz (opening the port pulses DTR/RTS -> one reboot -> clean baseline)
if [ -n "$PORT" ]; then
  python3 -u tools/usb_rate_check.py "$PORT" --seconds "$DUR" --mult 1 --boot-wait 8 > "$LOG/usb.log" 2>&1 &
  USB_PID=$!
  echo "[thermal] USB launched (scale rebooting) @ $(ts)"
fi

# 2) detect the reboot, then wait for full recovery
down=0
for i in $(seq 1 30); do
  if ! ping -c1 -t1 "$HOST" >/dev/null 2>&1; then down=1; echo "[thermal] reboot detected @ $(ts)"; break; fi
  sleep 1
done
until ping -c1 -t1 "$HOST" >/dev/null 2>&1; do sleep 1; done
sleep 4
echo "[thermal] WiFi back @ $(ts) (reboot_detected=$down) -- RECONNECT BT APP NOW"

RDUR=$((DUR-60)); [ "$RDUR" -lt 120 ] && RDUR=120

# 3) WiFi load
python3 -u tools/ws_drop_repro.py "$IP" --rate 10 --duration "$RDUR" --print-every 120 > "$LOG/ws.log" 2>&1 &
WS_PID=$!
python3 -u tools/conn_churn.py "$IP" --http --ws --rate 0.5 --workers 1 --duration "$RDUR" > "$LOG/churn.log" 2>&1 &
CHURN_PID=$!
python3 -u tools/mdns_stress.py --host "$HOST" --rate 1 --duration "$RDUR" --resolver > "$LOG/mdns.log" 2>&1 &
MDNS_PID=$!

# 4) telemetry monitor: one WS client, events on, log status every ~60 s.
#    Tracks peak temp / stalls / recoveries / reboots ACROSS the whole run (so a
#    firmware reset that zeroes the since-boot counters doesn't lose the peak),
#    and prints a SUMMARY + RESULT verdict at the end.
python3 -u - "$HOST" "$RDUR" > "$LOG/telemetry.log" 2>&1 <<'PY' &
import json,sys,time,websocket
host=sys.argv[1]; dur=int(sys.argv[2]); end=time.time()+dur
def connect():
    w=websocket.create_connection("ws://%s/snapshot"%host,timeout=8); w.settimeout(1.0)
    try:
        # events on -> enables periodic status broadcasts AND the debug event
        # broadcasts (stall_start/end, adc_recovery, temp_peak).
        w.send('{"command":"events","action":"on"}')
        # one-shot debug snapshot so we have soc_temp_c/peak/stall_count
        # populated from the start instead of waiting for an event firing.
        w.send('{"command":"debug"}')
    except Exception: pass
    return w
def merge(snap, msg):
    """Merge any frame's fields into the running snapshot.
    Returns ('status'|'debug:event'|'debug:snapshot'|'session_info'|None)."""
    try: d=json.loads(msg)
    except Exception: return None
    t=d.get('type')
    if t=='session_info':
        if 'reset_reason' in d: snap['reset_reason']=d['reset_reason']
        if 'firmware_version' in d: snap['firmware_version']=d['firmware_version']
        return 'session_info'
    if t=='debug':
        # event broadcasts carry only the delta; snapshot reply carries the full set
        for k in ('soc_temp_c','soc_temp_max_c','weight_stalled','stall_count',
                  'last_stall_ms','last_stall_temp_c','adc_recovery_count'):
            if k in d: snap[k]=d[k]
        ev=d.get('event')
        if ev=='stall_start': snap['weight_stalled']=True
        elif ev=='stall_end': snap['weight_stalled']=False
        return 'debug:'+ev if ev else 'debug:snapshot'
    if t=='status':
        for k in ('grams','charging','battery_percent','battery_voltage',
                  'timer_running','timer_seconds','display_on','low_power',
                  'soft_sleep','events_enabled','rate_hz','interval_ms'):
            if k in d: snap[k]=d[k]
        return 'status'
    return None
def drain(w, snap, secs):
    """Read frames for up to `secs`, merging into snap. Returns True if a status
    frame arrived (status is the 5 s periodic heartbeat -- its absence is the
    visibility-lost signal this test is hunting for)."""
    t_end=time.time()+secs; got_status=False
    while time.time()<t_end:
        try: kind=merge(snap, w.recv())
        except websocket.WebSocketTimeoutException: continue
        except Exception: break
        if kind=='status': got_status=True
    return got_status
ws=connect(); snap={}
peak=-999.0; total_stalls=0; reboots=0; max_recov=0
prev_stalls=None; prev_max=None; last_reset='?'
no_status_streak=0; max_no_status_streak=0; total_no_status=0
first=True
while time.time()<end:
    # poll a fresh debug snapshot every iteration so soc_temp_c / peak / stall
    # state stay current even when no event fires (event broadcasts are sparse
    # by design -- stall_start/end and temp_peak only fire on changes).
    try: ws.send('{"command":"debug"}')
    except Exception: pass
    got_status = drain(ws, snap, 9 if first else 2.5); first=False
    if got_status:
        no_status_streak=0
        soc=snap.get('soc_temp_c'); mx=snap.get('soc_temp_max_c')
        sc=snap.get('stall_count',0) or 0
        recov=snap.get('adc_recovery_count',0) or 0
        rr=snap.get('reset_reason','?'); last_reset=rr
        if isinstance(mx,(int,float)) and mx>peak: peak=mx
        if recov>max_recov: max_recov=recov
        # reboot heuristic: stall_count or peak dropped vs last poll. Both are
        # main-loop counters that zero on reboot (g_stallCount=0 in
        # parameter.h, g_socTempMaxC=-100 until first valid sample post-boot).
        if prev_stalls is not None and (sc<prev_stalls or (isinstance(mx,(int,float)) and isinstance(prev_max,(int,float)) and mx<prev_max-3)):
            reboots+=1
            print("[%s] *** REBOOT detected (counters reset; reset_reason=%s) ***"%(time.strftime('%H:%M:%S'),rr),flush=True)
        total_stalls=max(total_stalls, sc)
        prev_stalls=sc; prev_max=mx
        flag=" *** STALL ***" if snap.get('weight_stalled') else ""
        print("[%s] soc=%5sC max=%5sC stalled=%-5s stalls=%s recov=%s last_stall_ms=%s stall_temp=%s reset=%s grams=%s chg=%s%s"%(
            time.strftime('%H:%M:%S'), soc, mx, snap.get('weight_stalled'), sc, recov,
            snap.get('last_stall_ms'), snap.get('last_stall_temp_c'), rr, snap.get('grams'),
            snap.get('charging'), flag), flush=True)
    else:
        no_status_streak+=1; total_no_status+=1
        if no_status_streak>max_no_status_streak: max_no_status_streak=no_status_streak
        print("[%s] NO STATUS FRAME (reconnecting; streak=%d total=%d)"%(time.strftime('%H:%M:%S'),no_status_streak,total_no_status), flush=True)
        try: ws.close()
        except Exception: pass
        try: ws=connect(); first=True
        except Exception as e: print("reconnect failed:",e,flush=True); time.sleep(5)
    time.sleep(58)
try: ws.close()
except Exception: pass
# Loss of status frames means the scale stopped answering -- exactly the "weight
# stops" failure being hunted -- so it must FAIL, not silently PASS because the
# stall/reboot counters simply stopped advancing. Two patterns count: a SUSTAINED
# loss (streak >= 3 consecutive misses) AND a FLAPPING wedge that recovers on each
# reconnect (which resets the streak but accumulates total_no_status). A healthy
# hour-long run has ~0 misses (the 58 s sleep leaves a backlog of buffered status
# frames), so a cumulative threshold of 5 tolerates rare network jitter while
# still catching a scale that keeps dropping out.
visibility_lost = (max_no_status_streak >= 3) or (total_no_status >= 5)
# peak stays at its -999 sentinel if no soc_temp_max_c field was ever seen: the
# thermal data this test exists to capture is missing, so don't call it a PASS.
no_temp_data = peak < -900
result = "PASS" if (total_stalls==0 and max_recov==0 and reboots==0 and not visibility_lost and not no_temp_data) else "FAIL"
print("SUMMARY peak_temp=%.1fC total_stalls=%d adc_recoveries=%d reboots=%d max_no_status_streak=%d total_no_status=%d last_reset=%s RESULT=%s"%(
    peak, total_stalls, max_recov, reboots, max_no_status_streak, total_no_status, last_reset, result), flush=True)
PY
TELE_PID=$!

echo "[thermal] load + telemetry running ${RDUR}s @ $(ts)"
# Wait for each child individually so we capture its exit status (a bare `wait`
# discards them). These tools exit 0 on normal completion and non-zero only on a
# startup failure (missing dep / unresolvable host) or a crash/kill -- never on
# "detected drops" -- so a non-zero code reliably means that load generator did
# not do its job and the scale was under-stressed.
rc_usb=0; rc_ws=0; rc_churn=0; rc_mdns=0; rc_tele=0
[ -n "${USB_PID:-}" ] && { wait "$USB_PID"; rc_usb=$?; }
wait "$WS_PID";    rc_ws=$?
wait "$CHURN_PID"; rc_churn=$?
wait "$MDNS_PID";  rc_mdns=$?
wait "$TELE_PID";  rc_tele=$?
echo "[thermal] DONE $(ts)"
echo "===== TELEMETRY (last 12 + summary) ====="; tail -12 "$LOG/telemetry.log"
echo "----- key events -----"; grep -E "STALL|REBOOT|SUMMARY" "$LOG/telemetry.log" || echo "(none)"
echo "===== WS (drops) ====="; tail -12 "$LOG/ws.log"
echo "===== USB ====="; tail -6 "$LOG/usb.log" 2>/dev/null || echo "(USB disabled)"
echo "===== churn ====="; tail -3 "$LOG/churn.log"
echo "===== mDNS ====="; tail -3 "$LOG/mdns.log"

# Verdict -> exit code. A green run requires BOTH the telemetry monitor's
# RESULT=PASS *and* that every load generator stayed alive for the whole run: a
# generator that never started or crashed (non-zero exit) means the scale was
# under-stressed, so the run is invalid even if no stall was seen. A non-zero
# monitor exit or a missing SUMMARY line means the monitor itself died. This
# makes the script usable as a CI gate.
fail=0
if [ "$rc_tele" -ne 0 ] || ! grep -q "RESULT=PASS" "$LOG/telemetry.log"; then
  echo "[thermal] FAIL: telemetry verdict not PASS (stall/reboot/recovery, lost visibility, or monitor died rc=$rc_tele)"; fail=1
fi
check_gen() {  # $1=name $2=exit-code
  if [ "$2" -ne 0 ]; then
    echo "[thermal] FAIL: load generator '$1' exited $2 -- never started or crashed, scale under-stressed (see $LOG/$1.log)"; fail=1
  fi
}
[ -n "${USB_PID:-}" ] && check_gen usb "$rc_usb"
check_gen ws "$rc_ws"
check_gen churn "$rc_churn"
check_gen mdns "$rc_mdns"
[ "$fail" -eq 0 ] && echo "[thermal] RESULT=PASS" || echo "[thermal] RESULT=FAIL"
exit "$fail"
