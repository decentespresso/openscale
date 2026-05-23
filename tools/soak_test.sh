#!/bin/bash
# Realistic coexistence soak for the Half Decent Scale, toward the goal of
# >1 hour of 10 Hz WiFi scale data with no stream drops while BT is active.
#
# Concurrent load:
#   - WiFi:   one persistent 10 Hz /snapshot WS stream (ws_drop_repro)  [primary: WiFi drops]
#   - BT:     one persistent BLE link w/ heartbeat (ble_scale_client)   [BT load + regression: 0 drops]
#   - churn:  light HTTP+WS connect/disconnect activity (conn_churn)    ["real world" connection activity]
#   - serial: scale-side [wifi]/[health] logging (serial_tap)          [heap/wifi/disc trend]
#
# Usage: tools/soak_test.sh [IP] [DURATION_S] [CHURN_RATE]
#   tools/soak_test.sh 192.168.10.242 3600 0.3
set -u
IP="${1:-192.168.10.242}"
DUR="${2:-1800}"
CHURN="${3:-0.3}"
cd "$(dirname "$0")/.."

pkill -f serial_tap.py 2>/dev/null; sleep 1
ts() { date +%H:%M:%S; }
echo "[soak] START $(ts) ip=$IP dur=${DUR}s churn=${CHURN}/s"

( python3 -u tools/serial_tap.py --printable --out /tmp/soak_serial.log >/dev/null 2>&1 & )
python3 -u tools/ble_scale_client.py --duration "$DUR"      > /tmp/soak_ble.log   2>&1 &
BLE=$!
python3 -u tools/conn_churn.py "$IP" --http --ws --rate "$CHURN" --workers 1 --duration "$DUR" > /tmp/soak_churn.log 2>&1 &
CH=$!
# WS stream is the primary metric; run it in the foreground for the full duration.
python3 -u tools/ws_drop_repro.py "$IP" --duration "$DUR"  > /tmp/soak_ws.log    2>&1

kill "$BLE" "$CH" 2>/dev/null
pkill -f serial_tap.py
echo "[soak] DONE $(ts)"
echo "--- WiFi drops ---";  grep -c DROP /tmp/soak_ws.log 2>/dev/null
echo "--- BLE summary ---"; tail -4 /tmp/soak_ble.log 2>/dev/null
echo "--- serial wifi disc/rec + heap (last) ---"; grep health /tmp/soak_serial.log 2>/dev/null | tail -2
