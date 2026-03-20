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
from decode_ads_debug import decode_ads_debug_packet, print_debug_info

def calculate_checksum(data):
    """Calculate XOR checksum for command"""
    checksum = 0
    for byte in data:
        checksum ^= byte
    return checksum

def send_debug_command(ser, command_type):
    """
    Send debug command to scale
    
    Args:
        ser: Serial port object
        command_type: 0=OFF, 1=ON, 2=GET_INFO
    """
    cmd = bytes([0x03, 0x25, command_type])
    checksum = calculate_checksum(cmd)
    cmd += bytes([checksum])
    
    ser.write(cmd)
    
    cmd_names = {0: "DEBUG OFF", 1: "DEBUG ON", 2: "GET INFO"}
    print(f"Sent: {cmd_names.get(command_type, 'UNKNOWN')} ({' '.join(f'{b:02X}' for b in cmd)})")
    
    return command_type == 2  # Return True if we expect a response

def find_debug_packet(buffer):
    """
    Search buffer for debug packet (0x03 0x25 ... 41 bytes total)
    
    Returns:
        tuple: (packet_data, remaining_buffer) or (None, buffer)
    """
    # Look for packet start (0x03 0x25)
    for i in range(len(buffer) - 1):
        if buffer[i] == 0x03 and buffer[i+1] == 0x25:
            # Found potential packet start
            if len(buffer) >= i + 41:
                # We have enough bytes for a full packet
                packet = buffer[i:i+41]
                remaining = buffer[i+41:]
                return (packet, remaining)
            else:
                # Not enough bytes yet, keep buffer as is
                return (None, buffer)
    
    # No packet start found, keep last byte in case it's the start of 0x03
    if len(buffer) > 0:
        return (None, buffer[-1:])
    return (None, buffer)

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
        print(f"Buffer: {' '.join(f'{b:02X}' for b in buffer[:20])}...")
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
    print("  on      - Enable debug mode")
    print("  off     - Disable debug mode")
    print("  info    - Get debug info")
    print("  monitor - Start continuous monitoring")
    print("  quit    - Exit")
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
            elif cmd == "help":
                print("Commands: on, off, info, monitor, quit")
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
        """
    )
    
    parser.add_argument('port', help='Serial port (e.g., /dev/cu.wchusbserial10)')
    parser.add_argument('-b', '--baud', type=int, default=115200, help='Baud rate (default: 115200)')
    parser.add_argument('-m', '--monitor', action='store_true', help='Continuous monitoring mode')
    parser.add_argument('-i', '--interval', type=float, default=1.0, help='Monitor interval in seconds (default: 1.0)')
    parser.add_argument('--interactive', action='store_true', help='Interactive command mode')
    parser.add_argument('--debug-on', action='store_true', help='Enable debug mode and exit')
    parser.add_argument('--debug-off', action='store_true', help='Disable debug mode and exit')
    
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
        if args.debug_on:
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
