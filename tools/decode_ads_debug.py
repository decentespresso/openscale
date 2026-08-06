#!/usr/bin/env python3
"""
ADS1232 Debug Packet Decoder
Decodes the 41-byte debug packet sent by the scale firmware

Packet format (41 bytes total):
[0]    = 0x03 (model byte)
[1]    = 0x25 (debug packet type)
[2-5]  = timestamp (4 bytes, unsigned long)
[6-9]  = rawValue (4 bytes, signed long)
[10-13] = smoothedValue (4 bytes, signed long)
[14-17] = tareOffset (4 bytes, signed long)
[18-19] = conversionTime (2 bytes, float * 100)
[20-21] = sps (2 bytes, float * 100)
[22]    = readIndex (1 byte)
[23]    = samplesInUse (1 byte)
[24]    = resetReason (raw esp_reset_reason code captured at boot)
[25-37] = reserved (13 bytes)
[38]    = flags (bits: 0=dataOutOfRange, 1=signalTimeout)
[39]    = reserved (1 byte)
[40]    = checksum (XOR of bytes 0-39)

This module can be used standalone or imported by other scripts.
"""

import struct
import sys

from ads_debug_protocol import format_hex, xor_checksum

def decode_ads_debug_packet(data):
    if len(data) != 41:
        print(f"Error: Expected 41 bytes, got {len(data)}")
        return None

    checksum = xor_checksum(data[:40])

    if checksum != data[40]:
        print(f"Error: Checksum mismatch! Calculated: 0x{checksum:02X}, Got: 0x{data[40]:02X}")
        return None

    if data[0] != 0x03 or data[1] != 0x25:
        print(f"Error: Invalid header! Expected 0x03 0x25, got 0x{data[0]:02X} 0x{data[1]:02X}")
        return None

    info = {}

    info['timestamp'] = struct.unpack('>I', bytes(data[2:6]))[0]

    info['rawValue'] = struct.unpack('>i', bytes(data[6:10]))[0]

    info['smoothedValue'] = struct.unpack('>i', bytes(data[10:14]))[0]

    info['tareOffset'] = struct.unpack('>i', bytes(data[14:18]))[0]

    convTime = struct.unpack('>H', bytes(data[18:20]))[0]
    info['conversionTime'] = convTime / 100.0

    sps = struct.unpack('>H', bytes(data[20:22]))[0]
    info['sps'] = sps / 100.0

    info['readIndex'] = data[22]

    info['samplesInUse'] = data[23]

    info['resetReason'] = data[24]
    info['reserved'] = bytes(data[25:38])

    flags = data[38]
    info['dataOutOfRange'] = bool(flags & 0x01)
    info['signalTimeout'] = bool(flags & 0x02)
    info['reservedByte'] = data[39]

    return info

def print_debug_info(info):
    print("\n=== ADS1232 Debug Info ===")
    print(f"Timestamp:      {info['timestamp']} ms")
    print(f"Raw Value:      {info['rawValue']}")
    print(f"Smoothed:       {info['smoothedValue']}")
    print(f"Tare Offset:    {info['tareOffset']}")
    print(f"Conv Time:      {info['conversionTime']:.2f} ms")
    print(f"SPS:            {info['sps']:.2f}")
    print(f"Samples Used:   {info['samplesInUse']}")
    print(f"Read Index:     {info['readIndex']}")
    print(f"Reset Reason:   {info['resetReason']}")
    if any(info['reserved']) or info['reservedByte']:
        print(f"Reserved:       {format_hex(info['reserved'] + bytes([info['reservedByte']]))}")
    print(f"\nFlags:")
    print(f"  Out of Range: {info['dataOutOfRange']}")
    print(f"  Timeout:      {info['signalTimeout']}")
    print("==========================\n")

def decode_ads_reset_response(data):
    if len(data) != 5:
        print(f"Error: Expected 5 bytes, got {len(data)}")
        return None

    if data[0] != 0x03 or data[1] != 0x26:
        print(f"Error: Invalid header! Expected 0x03 0x26, got 0x{data[0]:02X} 0x{data[1]:02X}")
        return None

    checksum = xor_checksum(data[:4])
    if checksum != data[4]:
        print(f"Error: Checksum mismatch! Calculated: 0x{checksum:02X}, Got: 0x{data[4]:02X}")
        return None

    mode_names = {0x00: "Reset + refresh", 0x01: "Reset + refresh", 0x02: "Reset + refresh + tare complete"}

    return {
        'mode': data[2],
        'mode_name': mode_names.get(data[2], f"Unknown (0x{data[2]:02X})"),
        'status': data[3],
        'success': data[3] == 0x00,
    }

def print_reset_response(info):
    status_str = "SUCCESS" if info['success'] else "FAILED (DOUT timeout)"
    print(f"\n=== ADS1232 Reset Response ===")
    print(f"Mode:   0x{info['mode']:02X} ({info['mode_name']})")
    print(f"Status: {status_str}")
    print(f"==============================\n")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        hex_str = sys.argv[1].replace(" ", "").replace("0x", "")
        try:
            data = bytes.fromhex(hex_str)
            info = decode_ads_debug_packet(data)
            if info:
                print_debug_info(info)
            else:
                print("Failed to decode packet")
                sys.exit(1)
        except ValueError as e:
            print(f"Error: Invalid hex string: {e}")
            sys.exit(1)
    else:
        print("Usage:")
        print("  python decode_ads_debug.py <hex_string>")
        print("\nExample:")
        print("  python decode_ads_debug.py '03250000000100000001000000020000...'")
        print("\nOr import this module in another script:")
        print("  from decode_ads_debug import decode_ads_debug_packet, print_debug_info")
        sys.exit(1)
