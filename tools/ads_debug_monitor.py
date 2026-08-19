#!/usr/bin/env python3

import serial
import time
import sys
import argparse
from decode_ads_debug import decode_ads_debug_packet, print_debug_info, decode_ads_reset_response, print_reset_response
from ads_debug_protocol import (
    build_debug_command,
    build_reset_command,
    build_samples_command,
    find_debug_packet,
    find_reset_response,
    format_hex,
)

def send_debug_command(ser, command_type):
    cmd = build_debug_command(command_type)
    ser.write(cmd)

    cmd_names = {0: "DEBUG OFF", 1: "DEBUG ON", 2: "GET INFO"}
    print(f"Sent: {cmd_names.get(command_type, 'UNKNOWN')} ({format_hex(cmd)})")

    return command_type == 2

def send_reset_command(ser, mode):
    cmd = build_reset_command(mode)
    ser.write(cmd)

    mode_names = {0: "RESET + REFRESH", 1: "RESET + REFRESH", 2: "RESET + REFRESH + TARE COMPLETE"}
    print(f"Sent: {mode_names.get(mode, 'UNKNOWN')} ({format_hex(cmd)})")

def send_samples_command(ser, sample_count):
    cmd = build_samples_command(sample_count)
    ser.write(cmd)
    print(f"Sent: SET SAMPLES={sample_count} ({format_hex(cmd)})")

def read_serial_text(ser, timeout=1.0):
    start_time = time.time()
    output = bytearray()
    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting)
            output.extend(data)
        time.sleep(0.01)

    if output:
        try:
            text = output.decode('utf-8', errors='replace').strip()
            if text:
                print(f"Firmware: {text}")
        except Exception:
            print(f"Firmware (hex): {' '.join(f'{b:02X}' for b in output)}")

def read_reset_response(ser, timeout=5.0):
    buffer = bytearray()
    start_time = time.time()

    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting)
            buffer.extend(data)

            packet, buffer = find_reset_response(buffer)
            if packet:
                info = decode_ads_reset_response(packet)
                return info

        time.sleep(0.01)

    print(f"Timeout waiting for reset response (received {len(buffer)} bytes)")
    if len(buffer) > 0:
        print(f"Buffer: {format_hex(buffer[:20])}...")
    return None

def read_debug_packet(ser, timeout=2.0):
    buffer = bytearray()
    start_time = time.time()

    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting)
            buffer.extend(data)

            packet, buffer = find_debug_packet(buffer)
            if packet:
                info = decode_ads_debug_packet(packet)
                return info

        time.sleep(0.01)

    print(f"Timeout waiting for debug packet (received {len(buffer)} bytes)")
    if len(buffer) > 0:
        print(f"Buffer: {format_hex(buffer[:20])}...")
    return None

def monitor_mode(ser, interval=1.0):
    print("\n=== Continuous Monitor Mode ===")
    print(f"Requesting debug info every {interval} seconds")
    print("Press Ctrl+C to stop\n")

    try:
        while True:
            if ser.in_waiting > 0:
                ser.read(ser.in_waiting)

            send_debug_command(ser, 2)

            info = read_debug_packet(ser, timeout=1.0)
            if info:
                print_debug_info(info)
            else:
                print("Failed to receive debug packet\n")

            time.sleep(interval)

    except KeyboardInterrupt:
        print("\n\nMonitoring stopped by user")

def single_shot_mode(ser):
    if ser.in_waiting > 0:
        ser.read(ser.in_waiting)

    send_debug_command(ser, 2)

    info = read_debug_packet(ser, timeout=2.0)
    if info:
        print_debug_info(info)
        return True
    else:
        print("Failed to receive debug packet")
        return False

def interactive_mode(ser):
    print("\n=== Interactive Mode ===")
    print("Commands:")
    print("  on       - Enable debug mode")
    print("  off      - Disable debug mode")
    print("  info     - Get debug info")
    print("  monitor  - Start continuous monitoring")
    print("  reset 0    - ADS reset + refresh dataset")
    print("  reset 1    - ADS reset + refresh dataset")
    print("  reset 2    - ADS reset + refresh + completed tare")
    print("  samples N  - Set samples in use (1, 2, or 4)")
    print("  quit       - Exit")
    print()

    try:
        while True:
            cmd = input("debug> ").strip().lower()

            if cmd == "quit" or cmd == "exit" or cmd == "q":
                break
            elif cmd == "on":
                send_debug_command(ser, 1)  # DEBUG ON
                print("Debug mode enabled\n")
            elif cmd == "off":
                send_debug_command(ser, 0)  # DEBUG OFF
                print("Debug mode disabled\n")
            elif cmd == "info":
                send_debug_command(ser, 2)
                info = read_debug_packet(ser, timeout=2.0)
                if info:
                    print_debug_info(info)
                else:
                    print("Failed to receive debug packet\n")
            elif cmd == "monitor":
                interval = input("Interval in seconds (default 1.0): ").strip()
                try:
                    interval = float(interval) if interval else 1.0
                except ValueError:
                    interval = 1.0
                monitor_mode(ser, interval)
            elif cmd.startswith("reset"):
                parts = cmd.split()
                if len(parts) == 2 and parts[1] in ("0", "1", "2"):
                    mode = int(parts[1])
                    if ser.in_waiting > 0:
                        ser.read(ser.in_waiting)
                    send_reset_command(ser, mode)
                    print("Waiting for response...")
                    info = read_reset_response(ser, timeout=5.0)
                    if info:
                        print_reset_response(info)
                    else:
                        print("No response received\n")
                else:
                    print("Usage: reset <0|1|2>")
                    print("  0 = Reset + refresh dataset")
                    print("  1 = Reset + refresh dataset")
                    print("  2 = Reset + refresh + completed tare\n")
            elif cmd.startswith("samples"):
                parts = cmd.split()
                if len(parts) == 2 and parts[1] in ("1", "2", "4"):
                    if ser.in_waiting > 0:
                        ser.read(ser.in_waiting)
                    send_samples_command(ser, int(parts[1]))
                    read_serial_text(ser)
                else:
                    print("Usage: samples <1|2|4>\n")
            elif cmd == "help":
                print("Commands: on, off, info, monitor, reset <0|1|2>, samples <1|2|4>, quit")
            elif cmd == "":
                continue
            else:
                print(f"Unknown command: {cmd}")
                print("Type 'help' for available commands\n")

    except KeyboardInterrupt:
        print("\n\nExiting...")
    except EOFError:
        print("\n\nExiting...")

def main():
    parser = argparse.ArgumentParser(
        description="ADS1232 Debug Monitor - Send debug commands and view diagnostics",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Single debug info request
  python ads_debug_monitor.py /dev/cu.wchusbserial10

  # Continuous monitoring every 2 seconds
  python ads_debug_monitor.py /dev/cu.wchusbserial10 --monitor --interval 2

  # Interactive mode
  python ads_debug_monitor.py /dev/cu.wchusbserial10 --interactive

  # Enable debug mode and exit
  python ads_debug_monitor.py /dev/cu.wchusbserial10 --debug-on

  # ADS reset + refresh dataset
  python ads_debug_monitor.py /dev/cu.wchusbserial10 --reset 0

  # ADS reset + refresh + completed tare
  python ads_debug_monitor.py /dev/cu.wchusbserial10 --reset 2

  # Set samples in use to 1
  python ads_debug_monitor.py /dev/cu.wchusbserial10 --samples 1
        """
    )

    parser.add_argument('port', help='Serial port (e.g., /dev/cu.wchusbserial10)')
    parser.add_argument('-b', '--baud', type=int, default=115200, help='Baud rate (default: 115200)')
    parser.add_argument('-m', '--monitor', action='store_true', help='Continuous monitoring mode')
    parser.add_argument('-i', '--interval', type=float, default=1.0, help='Monitor interval in seconds (default: 1.0)')
    parser.add_argument('--interactive', action='store_true', help='Interactive command mode')
    parser.add_argument('--debug-on', action='store_true', help='Enable debug mode and exit')
    parser.add_argument('--debug-off', action='store_true', help='Disable debug mode and exit')
    parser.add_argument('--reset', type=int, choices=[0, 1, 2], metavar='MODE',
                        help='ADS reset: 0=refresh, 1=refresh, 2=refresh+tare complete')
    parser.add_argument('--samples', type=int, choices=[1, 2, 4], metavar='N',
                        help='Set samples in use (1, 2, or 4)')

    args = parser.parse_args()

    try:
        print(f"Opening {args.port} at {args.baud} baud...")
        ser = serial.Serial(args.port, args.baud, timeout=1)
        time.sleep(0.5)
        print("Connected.\n")
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        sys.exit(1)

    try:
        if args.samples is not None:
            if ser.in_waiting > 0:
                ser.read(ser.in_waiting)
            send_samples_command(ser, args.samples)
            read_serial_text(ser)
        elif args.reset is not None:
            if ser.in_waiting > 0:
                ser.read(ser.in_waiting)
            send_reset_command(ser, args.reset)
            print("Waiting for response...")
            info = read_reset_response(ser, timeout=5.0)
            if info:
                print_reset_response(info)
                sys.exit(0 if info['success'] else 1)
            else:
                print("No response received")
                sys.exit(1)
        elif args.debug_on:
            send_debug_command(ser, 1)  # DEBUG ON
            print("Debug mode enabled")
        elif args.debug_off:
            send_debug_command(ser, 0)  # DEBUG OFF
            print("Debug mode disabled")
        elif args.interactive:
            interactive_mode(ser)
        elif args.monitor:
            monitor_mode(ser, args.interval)
        else:
            success = single_shot_mode(ser)
            sys.exit(0 if success else 1)

    finally:
        ser.close()
        print("Serial port closed.")

if __name__ == "__main__":
    main()
