#!/usr/bin/env python3
"""
mDNS responder stressor for the Half Decent Scale.

Hammers the scale's ESPmDNS responder while it streams 10 Hz over WiFi, to make
sure discovery traffic (which clients generate constantly) doesn't disturb the
stream or wedge the responder. Sends mDNS queries to 224.0.0.251:5353 with the
unicast-response (QU) bit and measures replies; with --resolver it also exercises
the OS resolver the way a real client's discovery does (this is what failed with
gaierror under load earlier).

Queries:
  A   hds.local
  PTR _decentscale._tcp.local

Usage:
  python3 tools/mdns_stress.py --rate 3 --resolver --duration 3800
"""

import argparse
import socket
import struct
import time
from datetime import datetime

MCAST = ("224.0.0.251", 5353)


def ts():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def build_query(name, qtype, qu=True):
    header = struct.pack(">HHHHHH", 0, 0, 1, 0, 0, 0)  # id0, flags0, qd1
    q = b""
    for part in name.split("."):
        if part:
            q += bytes([len(part)]) + part.encode()
    q += b"\x00"
    q += struct.pack(">HH", qtype, 0x8001 if qu else 0x0001)  # QU bit + IN
    return header + q


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="hds.local")
    ap.add_argument("--service", default="_decentscale._tcp.local")
    ap.add_argument("--rate", type=float, default=3.0)
    ap.add_argument("--duration", type=float, default=0)
    ap.add_argument("--timeout", type=float, default=1.0)
    ap.add_argument("--resolver", action="store_true",
                    help="also getaddrinfo(host) each cycle (client-observable mDNS resolution)")
    args = ap.parse_args()

    queries = [("A", build_query(args.host, 1)), ("PTR", build_query(args.service, 12))]
    sent = replied = lost = 0
    lat = []
    res_ok = res_fail = 0
    started = time.monotonic()
    i = 0
    print(f"[{ts()}] mdns stress: host={args.host} svc={args.service} rate={args.rate}/s "
          f"resolver={args.resolver}", flush=True)

    while not args.duration or (time.monotonic() - started) < args.duration:
        t0 = time.monotonic()
        _label, q = queries[i % 2]
        i += 1
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except OSError:
            pass
        s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
        s.bind(("0.0.0.0", 0))
        s.settimeout(args.timeout)
        try:
            s.sendto(q, MCAST)
            sent += 1
            s.recvfrom(2048)  # any mDNS reply
            replied += 1
            lat.append((time.monotonic() - t0) * 1000)
        except socket.timeout:
            lost += 1
        except OSError:
            lost += 1
        finally:
            s.close()

        if args.resolver:
            r0 = time.monotonic()
            try:
                socket.getaddrinfo(args.host, 80)
                res_ok += 1
            except OSError:
                res_fail += 1

        if sent % 20 == 0:
            avg = f"{sum(lat)/len(lat):.0f}ms" if lat else "n/a"
            extra = f" | resolver ok={res_ok} fail={res_fail}" if args.resolver else ""
            print(f"[{ts()}] mdns sent={sent} replied={replied} lost={lost} avg_lat={avg}{extra}",
                  flush=True)

        dt = time.monotonic() - t0
        if args.rate > 0 and dt < 1.0 / args.rate:
            time.sleep(1.0 / args.rate - dt)

    avg = f"{sum(lat)/len(lat):.0f}ms" if lat else "n/a"
    extra = f" | resolver ok={res_ok} fail={res_fail}" if args.resolver else ""
    print(f"\n[{ts()}] MDNS SUMMARY sent={sent} replied={replied} lost={lost} "
          f"loss={100*lost/max(sent,1):.0f}% avg_lat={avg}{extra}", flush=True)


if __name__ == "__main__":
    main()
