#!/usr/bin/env python3
"""
ADS1232 Debug Monitor
Connects to the scale via serial, sends debug commands, and displays results.

This script handles serial communication and uses decode_ads_debug.py for packet parsing.
"""

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
    """
    Send debug command to scale
    
    Args:
        ser: Serial port object
        command_type: 0=OFF, 1=ON, 2=GET_INFO
    """
    cmd = build_debug_command(command_type)
    ser.write(cmd)
    
    cmd_names = {0: "DEBUG OFF", 1: "DEBUG ON", 2: "GET INFO"}
    print(f"Sent: {cmd_names.get(command_type, 'UNKNOWN')} ({format_hex(cmd)})")
    
    return command_type == 2  # Return True if we expect a response

def send_reset_command(ser, mode):
    """
    Send ADS reset command to scale
    
    Args:
        ser: Serial port object
        mode: 0=reset+refresh, 1=reset+refresh, 2=reset+refresh+tare complete
    """
    cmd = build_reset_command(mode)
    ser.write(cmd)
    
    mode_names = {0: "RESET + REFRESH", 1: "RESET + REFRESH", 2: "RESET + REFRESH + TARE COMPLETE"}
    print(f"Sent: {mode_names.get(mode, 'UNKNOWN')} ({format_hex(cmd)})")

def send_samples_command(ser, sample_count):
    """
    Send samples-in-use command to scale
    
    Args:
        ser: Serial port object
        sample_count: 1, 2, or 4
    """
    cmd = build_samples_command(sample_count)
    ser.write(cmd)
    print(f"Sent: SET SAMPLES={sample_count} ({format_hex(cmd)})")

def read_serial_text(ser, timeout=1.0):
    """
    Read and print any text/raw data from serial for a given duration.
    Useful for seeing firmware log output after fire-and-forget commands.
    
    Args:
        ser: Serial port object
        timeout: How long to listen in seconds
    """
    start_time = time.time()
    output = bytearray()
    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting)
            output.extend(data)
        time.sleep(0.01)
    
    if output:
        # Try to decode as text, fall back to hex
        try:
            text = output.decode('utf-8', errors='replace').strip()
            if text:
                print(f"Firmware: {text}")
        except Exception:
            print(f"Firmware (hex): {' '.join(f'{b:02X}' for b in output)}")

def read_reset_response(ser, timeout=5.0):
    """
    Read and decode reset response from serial port.
    Uses longer default timeout since reset includes 500ms power-down + 500ms DOUT wait.
    
    Args:
        ser: Serial port object
        timeout: Timeout in seconds
        
    Returns:
        dict: Decoded reset response or None if timeout/error
    """
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
    """
    Read and decode debug packet from serial port
    
    Args:
        ser: Serial port object
        timeout: Timeout in seconds
        
    Returns:
        dict: Decoded debug info or None if timeout/error
    """
    buffer = bytearray()
    start_time = time.time()
    
    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting)
            buffer.extend(data)
            
            # Try to find a packet
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
    """
    Continuously request and display debug info
    
    Args:
        ser: Serial port object
        interval: Time between requests in seconds
    """
    print("\n=== Continuous Monitor Mode ===")
    print(f"Requesting debug info every {interval} seconds")
    print("Press Ctrl+C to stop\n")
    
    try:
        while True:
            # Clear any pending data
            if ser.in_waiting > 0:
                ser.read(ser.in_waiting)
            
            # Request debug info
            send_debug_command(ser, 2)  # GET_INFO
            
            # Read and display response
            info = read_debug_packet(ser, timeout=1.0)
            if info:
                print_debug_info(info)
            else:
                print("Failed to receive debug packet\n")
            
            time.sleep(interval)
            
    except KeyboardInterrupt:
        print("\n\nMonitoring stopped by user")

def single_shot_mode(ser):
    """
    Request debug info once and exit
    
    Args:
        ser: Serial port object
    """
    # Clear any pending data
    if ser.in_waiting > 0:
        ser.read(ser.in_waiting)
    
    # Request debug info
    send_debug_command(ser, 2)  # GET_INFO
    
    # Read and display response
    info = read_debug_packet(ser, timeout=2.0)
    if info:
        print_debug_info(info)
        return True
    else:
        print("Failed to receive debug packet")
        return False

def interactive_mode(ser):
    """
    Interactive command mode
    
    Args:
        ser: Serial port object
    """
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
                send_debug_command(ser, 2)  # GET_INFO
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
    
    # Open serial port
    try:
        print(f"Opening {args.port} at {args.baud} baud...")
        ser = serial.Serial(args.port, args.baud, timeout=1)
        time.sleep(0.5)  # Wait for connection to stabilize
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
            # Default: single shot mode
            success = single_shot_mode(ser)
            sys.exit(0 if success else 1)
            
    finally:
        ser.close()
        print("Serial port closed.")

if __name__ == "__main__":
    main()
