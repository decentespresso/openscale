#!/bin/bash
# 30-min post-fix regression soak. Exercises every path the review fixes touched:
#   - WiFi WS 10 Hz persistent stream  (drops/Hz)          [primary]
#   - USB 10 Hz binary stream          (unified tick)       [opens serial -> 1 reboot]
#   - HTTP + WS connection churn       (PCB pool, heap watchdog, reconnect)
#   - mDNS resolution under load       (deferred main-loop advertise)
#   - concurrent web-app asset bursts  (HTTP token bucket while streaming)
set -u
cd /Users/jeffreyh/Development/openscale
IP=192.168.10.242
HOST=hds.local
DUR="${1:-1800}"
PORT=/dev/cu.wchusbserial110
LOG=/tmp/stress2
rm -rf "$LOG"; mkdir -p "$LOG"
ts(){ date +%H:%M:%S; }
echo "[stress] START $(ts) dur=${DUR}s  WS10Hz + USB10Hz + HTTP/WS churn + mDNS + token-bucket bursts"

# 1) USB stream (opening the port pulses DTR/RTS -> reboots the scale; hold for the run)
python3 -u tools/usb_rate_check.py "$PORT" --seconds "$DUR" --mult 1 --boot-wait 8 > "$LOG/usb.log" 2>&1 &
USB=$!
echo "[stress] USB stream launched (scale rebooting) @ $(ts)"

# 2) Detect the USB-induced reboot, then wait for full recovery, so the WS drop
#    metric isn't contaminated by the startup reboot. Opening the port pulses
#    DTR/RTS a moment after launch, so first wait (bounded) for the link to drop.
down=0
for i in $(seq 1 30); do
  if ! ping -c1 -t1 "$HOST" >/dev/null 2>&1; then down=1; echo "[stress] scale down (reboot detected) @ $(ts)"; break; fi
  sleep 1
done
until ping -c1 -t1 "$HOST" >/dev/null 2>&1; do sleep 1; done
sleep 3   # let the WS server + mDNS finish coming up
echo "[stress] WiFi back & settled @ $(ts) (reboot_detected=$down)"

RDUR=$((DUR-40)); [ "$RDUR" -lt 120 ] && RDUR=120

# 3) WS 10 Hz persistent stream (primary drop metric)
python3 -u tools/ws_drop_repro.py "$IP" --rate 10 --duration "$RDUR" --print-every 60 > "$LOG/ws.log" 2>&1 &
WS=$!
# 4) HTTP + WS connection churn
python3 -u tools/conn_churn.py "$IP" --http --ws --rate 0.5 --workers 1 --duration "$RDUR" > "$LOG/churn.log" 2>&1 &
CH=$!
# 5) mDNS stress incl. client-observable getaddrinfo
python3 -u tools/mdns_stress.py --host "$HOST" --rate 1 --duration "$RDUR" --resolver > "$LOG/mdns.log" 2>&1 &
MD=$!
echo "[stress] WiFi load (WS+churn+mDNS) running ${RDUR}s @ $(ts)"

ASSETS=(/ /dosing_assistant/dosing_assistant.html /dosing_assistant/same_weight.css \
 /dosing_assistant/main.js /dosing_assistant/modules/constants.js /dosing_assistant/modules/dosing.js \
 /shared/modules/presets.js /dosing_assistant/modules/export.js \
 /dosing_assistant/modules/state-machine.js /dosing_assistant/modules/ui-controller.js \
 /dosing_assistant/modules/scale.js /shared/reconnecting-websocket.js)

END=$(( $(date +%s) + RDUR ))
while [ "$(date +%s)" -lt "$END" ]; do
  sleep 300
  # concurrent asset burst (browser-like) while the WS stream is active -> token bucket
  : > "$LOG/burst.codes"
  for p in "${ASSETS[@]}"; do
    ( curl -s -o /dev/null -w "%{http_code}\n" --max-time 6 "http://$HOST$p" >> "$LOG/burst.codes" ) &
  done
  wait
  ok=$(grep -c '^200$' "$LOG/burst.codes"); c503=$(grep -c '^503$' "$LOG/burst.codes"); tot=$(wc -l < "$LOG/burst.codes" | tr -d ' ')
  wsline=$(grep -iE 'hz|drop|stall' "$LOG/ws.log" | tail -1)
  echo "[milestone $(ts)] assets ${ok}/${tot} 200 (${c503}x503) | reach=$(ping -c1 -t1 $HOST >/dev/null 2>&1 && echo OK || echo FAIL) | WS: ${wsline}"
done

kill "$WS" "$CH" "$MD" "$USB" 2>/dev/null
echo "[stress] DONE $(ts)"
echo "===== WS (drops/Hz) ====="; tail -20 "$LOG/ws.log"
echo "===== USB ====="; tail -12 "$LOG/usb.log"
echo "===== churn ====="; tail -8 "$LOG/churn.log"
echo "===== mDNS ====="; tail -8 "$LOG/mdns.log"
