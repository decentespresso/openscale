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
[24-27] = dataMin (4 bytes, signed long)
[28-31] = dataMax (4 bytes, signed long)
[32-35] = dataAvg (4 bytes, signed long)
[36-37] = dataStdDev (2 bytes, float * 10)
[38]    = flags (bits: 0=dataOutOfRange, 1=signalTimeout, 2=tareInProgress)
[39]    = tareTimes (1 byte)
[40]    = checksum (XOR of bytes 0-39)

This module can be used standalone or imported by other scripts.
"""

import struct
import sys

def decode_ads_debug_packet(data):
    """Decode a 41-byte ADS debug packet"""
    if len(data) != 41:
        print(f"Error: Expected 41 bytes, got {len(data)}")
        return None
    
    # Verify checksum
    checksum = 0
    for i in range(40):
        checksum ^= data[i]
    
    if checksum != data[40]:
        print(f"Error: Checksum mismatch! Calculated: 0x{checksum:02X}, Got: 0x{data[40]:02X}")
        return None
    
    # Verify header
    if data[0] != 0x03 or data[1] != 0x25:
        print(f"Error: Invalid header! Expected 0x03 0x25, got 0x{data[0]:02X} 0x{data[1]:02X}")
        return None
    
    # Decode fields
    info = {}
    
    # Timestamp (4 bytes, big-endian unsigned)
    info['timestamp'] = struct.unpack('>I', bytes(data[2:6]))[0]
    
    # Raw value (4 bytes, big-endian signed)
    info['rawValue'] = struct.unpack('>i', bytes(data[6:10]))[0]
    
    # Smoothed value (4 bytes, big-endian signed)
    info['smoothedValue'] = struct.unpack('>i', bytes(data[10:14]))[0]
    
    # Tare offset (4 bytes, big-endian signed)
    info['tareOffset'] = struct.unpack('>i', bytes(data[14:18]))[0]
    
    # Conversion time (2 bytes, big-endian unsigned, divide by 100)
    convTime = struct.unpack('>H', bytes(data[18:20]))[0]
    info['conversionTime'] = convTime / 100.0
    
    # SPS (2 bytes, big-endian unsigned, divide by 100)
    sps = struct.unpack('>H', bytes(data[20:22]))[0]
    info['sps'] = sps / 100.0
    
    # Read index (1 byte)
    info['readIndex'] = data[22]
    
    # Samples in use (1 byte)
    info['samplesInUse'] = data[23]
    
    # Data min (4 bytes, big-endian signed)
    info['dataMin'] = struct.unpack('>i', bytes(data[24:28]))[0]
    
    # Data max (4 bytes, big-endian signed)
    info['dataMax'] = struct.unpack('>i', bytes(data[28:32]))[0]
    
    # Data avg (4 bytes, big-endian signed)
    info['dataAvg'] = struct.unpack('>i', bytes(data[32:36]))[0]
    
    # StdDev (2 bytes, big-endian unsigned, divide by 10)
    stdDev = struct.unpack('>H', bytes(data[36:38]))[0]
    info['dataStdDev'] = stdDev / 10.0
    
    # Flags (1 byte)
    flags = data[38]
    info['dataOutOfRange'] = bool(flags & 0x01)
    info['signalTimeout'] = bool(flags & 0x02)
    info['tareInProgress'] = bool(flags & 0x04)
    
    # Tare times (1 byte)
    info['tareTimes'] = data[39]
    
    return info

def print_debug_info(info):
    """Pretty print debug info"""
    print("\n=== ADS1232 Debug Info ===")
    print(f"Timestamp:      {info['timestamp']} ms")
    print(f"Raw Value:      {info['rawValue']}")
    print(f"Smoothed:       {info['smoothedValue']}")
    print(f"Tare Offset:    {info['tareOffset']}")
    print(f"Conv Time:      {info['conversionTime']:.2f} ms")
    print(f"SPS:            {info['sps']:.2f}")
    print(f"Samples Used:   {info['samplesInUse']}")
    print(f"Read Index:     {info['readIndex']}")
    print(f"\nDataset Stats:")
    print(f"  Min:          {info['dataMin']}")
    print(f"  Max:          {info['dataMax']}")
    print(f"  Avg:          {info['dataAvg']}")
    print(f"  Range:        {info['dataMax'] - info['dataMin']}")
    print(f"  Std Dev:      {info['dataStdDev']:.1f} (lower = more stable)")
    print(f"\nFlags:")
    print(f"  Out of Range: {info['dataOutOfRange']}")
    print(f"  Timeout:      {info['signalTimeout']}")
    print(f"  Tare Active:  {info['tareInProgress']}", end='')
    if info['tareInProgress']:
        print(f" ({info['tareTimes']} samples)")
    else:
        print()
    print("==========================\n")

def decode_ads_reset_response(data):
    """Decode a 5-byte ADS reset response packet
    
    Packet format (5 bytes):
    [0] = 0x03 (model byte)
    [1] = 0x26 (reset response type)
    [2] = mode (0x00=soft, 0x01=refresh, 0x02=refresh+tare)
    [3] = status (0x00=success, 0x01=fail)
    [4] = checksum (XOR of bytes 0-3)
    """
    if len(data) != 5:
        print(f"Error: Expected 5 bytes, got {len(data)}")
        return None
    
    # Verify header
    if data[0] != 0x03 or data[1] != 0x26:
        print(f"Error: Invalid header! Expected 0x03 0x26, got 0x{data[0]:02X} 0x{data[1]:02X}")
        return None
    
    # Verify checksum
    checksum = data[0] ^ data[1] ^ data[2] ^ data[3]
    if checksum != data[4]:
        print(f"Error: Checksum mismatch! Calculated: 0x{checksum:02X}, Got: 0x{data[4]:02X}")
        return None
    
    mode_names = {0x00: "Soft reset", 0x01: "Reset + refresh", 0x02: "Reset + refresh + tare"}
    
    return {
        'mode': data[2],
        'mode_name': mode_names.get(data[2], f"Unknown (0x{data[2]:02X})"),
        'status': data[3],
        'success': data[3] == 0x00,
    }

def print_reset_response(info):
    """Pretty print reset response"""
    status_str = "SUCCESS" if info['success'] else "FAILED (DOUT timeout)"
    print(f"\n=== ADS1232 Reset Response ===")
    print(f"Mode:   0x{info['mode']:02X} ({info['mode_name']})")
    print(f"Status: {status_str}")
    print(f"==============================\n")


if __name__ == "__main__":
    # When run directly, decode from stdin or command line
    if len(sys.argv) > 1:
        # Decode hex string from command line
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


