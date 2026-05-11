#!/usr/bin/env python3
"""
ADS1232 Debug Monitor (BLE)
Companion to ads_debug_monitor.py that talks to the scale over BLE
instead of USB serial. Uses the same 0x25 command and 41-byte packet
format, decoded with decode_ads_debug.py.

BLE GATT layout (Decent Scale):
  Service:   0000fff0-0000-1000-8000-00805f9b34fb
  Write:     0000 36f5-0000-1000-8000-00805f9b34fb
  Notify:    0000fff4-0000-1000-8000-00805f9b34fb

Commands (write to 36f5):
  03 25 00 <xor>   -> debug streaming OFF
  03 25 01 <xor>   -> debug streaming CONTINUOUS (~10 Hz)
  03 25 02 <xor>   -> debug streaming SINGLE (one notify, auto-clears)

Requires: pip install bleak
"""

import argparse
import asyncio
import sys
import time

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    print("Error: bleak not installed. Run: pip install bleak", file=sys.stderr)
    sys.exit(1)

from decode_ads_debug import decode_ads_debug_packet, print_debug_info

SERVICE_UUID = "0000fff0-0000-1000-8000-00805f9b34fb"
WRITE_UUID   = "000036f5-0000-1000-8000-00805f9b34fb"
NOTIFY_UUID  = "0000fff4-0000-1000-8000-00805f9b34fb"

DEFAULT_NAME = "Decent Scale"


def xor_checksum(data):
    c = 0
    for b in data:
        c ^= b
    return c


def build_debug_command(mode):
    """mode: 0=OFF, 1=CONTINUOUS, 2=SINGLE"""
    payload = bytes([0x03, 0x25, mode])
    return payload + bytes([xor_checksum(payload)])


async def find_device(name_filter, timeout):
    print(f"Scanning for BLE devices (filter='{name_filter}', timeout={timeout}s)...")
    devices = await BleakScanner.discover(timeout=timeout)
    matches = [d for d in devices if d.name and name_filter.lower() in d.name.lower()]
    if not matches:
        print("No matching device found. Visible devices:")
        for d in devices:
            print(f"  {d.address}  {d.name}")
        return None
    if len(matches) > 1:
        print(f"Multiple matches, using first:")
        for d in matches:
            print(f"  {d.address}  {d.name}")
    chosen = matches[0]
    print(f"Selected: {chosen.address}  {chosen.name}")
    return chosen.address


class BleMonitor:
    def __init__(self, address):
        self.address = address
        self.client = None
        # Notify packets are 41 bytes and fit a single MTU; assume one notify == one packet
        self._packet_handler = None

    async def __aenter__(self):
        self.client = BleakClient(self.address)
        await self.client.connect()
        if not self.client.is_connected:
            raise RuntimeError(f"Failed to connect to {self.address}")
        print(f"Connected to {self.address}")
        await self.client.start_notify(NOTIFY_UUID, self._on_notify)
        return self

    async def __aexit__(self, exc_type, exc, tb):
        try:
            await self.client.stop_notify(NOTIFY_UUID)
        except Exception:
            pass
        await self.client.disconnect()
        print("Disconnected.")

    def _on_notify(self, _char, data):
        # Filter to debug packets only (header 0x03 0x25, length 41).
        # Other notifications on fff4 (weight, voltage, etc.) are ignored.
        if len(data) == 41 and data[0] == 0x03 and data[1] == 0x25:
            info = decode_ads_debug_packet(bytes(data))
            if info and self._packet_handler:
                self._packet_handler(info)

    async def send(self, mode):
        cmd = build_debug_command(mode)
        mode_names = {0: "OFF", 1: "CONTINUOUS", 2: "SINGLE"}
        print(f"Sent: {mode_names.get(mode, '?')} ({' '.join(f'{b:02X}' for b in cmd)})")
        await self.client.write_gatt_char(WRITE_UUID, cmd, response=False)

    def set_handler(self, handler):
        self._packet_handler = handler


async def single_shot(address):
    async with BleMonitor(address) as mon:
        received = asyncio.Event()

        def handler(info):
            print_debug_info(info)
            received.set()

        mon.set_handler(handler)
        await mon.send(2)  # SINGLE
        try:
            await asyncio.wait_for(received.wait(), timeout=3.0)
            return True
        except asyncio.TimeoutError:
            print("Timeout waiting for debug packet")
            return False


async def monitor(address, duration=None):
    async with BleMonitor(address) as mon:
        count = 0
        last_print = time.time()

        def handler(info):
            nonlocal count, last_print
            count += 1
            now = time.time()
            # Print every 1s to avoid flooding terminal at ~10 Hz
            if now - last_print >= 1.0:
                print_debug_info(info)
                print(f"({count} packets received)\n")
                last_print = now

        mon.set_handler(handler)
        await mon.send(1)  # CONTINUOUS
        print("Streaming. Press Ctrl+C to stop." if duration is None
              else f"Streaming for {duration}s...")
        try:
            if duration is None:
                while True:
                    await asyncio.sleep(0.5)
            else:
                await asyncio.sleep(duration)
        except (KeyboardInterrupt, asyncio.CancelledError):
            print("\nStopping...")
        finally:
            try:
                await mon.send(0)  # OFF
            except Exception:
                pass
        print(f"Total packets: {count}")


async def interactive(address):
    async with BleMonitor(address) as mon:
        latest = {"info": None}

        def handler(info):
            latest["info"] = info
            print_debug_info(info)

        mon.set_handler(handler)
        print("\n=== Interactive Mode (BLE) ===")
        print("  on       - Enable continuous streaming")
        print("  off      - Disable streaming")
        print("  info     - Request single packet")
        print("  quit     - Exit\n")

        loop = asyncio.get_event_loop()
        try:
            while True:
                cmd = (await loop.run_in_executor(None, input, "ble> ")).strip().lower()
                if cmd in ("quit", "exit", "q"):
                    await mon.send(0)
                    break
                elif cmd == "on":
                    await mon.send(1)
                elif cmd == "off":
                    await mon.send(0)
                elif cmd == "info":
                    await mon.send(2)
                elif cmd == "":
                    continue
                else:
                    print("Commands: on, off, info, quit")
        except (KeyboardInterrupt, EOFError):
            print()
            try:
                await mon.send(0)
            except Exception:
                pass


async def run(args):
    address = args.address
    if address is None:
        address = await find_device(args.name, args.scan_timeout)
        if address is None:
            sys.exit(1)

    if args.debug_on:
        async with BleMonitor(address) as mon:
            await mon.send(1)
    elif args.debug_off:
        async with BleMonitor(address) as mon:
            await mon.send(0)
    elif args.interactive:
        await interactive(address)
    elif args.monitor:
        await monitor(address, args.duration)
    else:
        ok = await single_shot(address)
        sys.exit(0 if ok else 1)


def main():
    parser = argparse.ArgumentParser(
        description="ADS1232 Debug Monitor (BLE) - HDS Doctor over BLE",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Auto-scan for "Decent Scale" and request a single packet
  python ads_debug_monitor_ble.py

  # Specify BLE address directly
  python ads_debug_monitor_ble.py --address AA:BB:CC:DD:EE:FF

  # Stream continuously, print ~once per second, OFF on Ctrl+C
  python ads_debug_monitor_ble.py --monitor

  # Stream for 10 seconds and exit
  python ads_debug_monitor_ble.py --monitor --duration 10

  # Interactive mode
  python ads_debug_monitor_ble.py --interactive

Requires: pip install bleak
        """,
    )
    parser.add_argument("--address", help="BLE address (skips scan)")
    parser.add_argument("--name", default=DEFAULT_NAME, help=f"Name filter for scan (default: '{DEFAULT_NAME}')")
    parser.add_argument("--scan-timeout", type=float, default=5.0, help="Scan timeout in seconds (default: 5)")
    parser.add_argument("-m", "--monitor", action="store_true", help="Continuous streaming mode")
    parser.add_argument("--duration", type=float, help="Stop monitor after N seconds (default: until Ctrl+C)")
    parser.add_argument("--interactive", action="store_true", help="Interactive command mode")
    parser.add_argument("--debug-on", action="store_true", help="Start CONTINUOUS streaming and exit (stream keeps going on device)")
    parser.add_argument("--debug-off", action="store_true", help="Stop streaming and exit")
    args = parser.parse_args()

    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
