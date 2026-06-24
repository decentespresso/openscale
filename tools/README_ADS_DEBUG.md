# ADS1232 Debug System

This document describes the debug system for the ADS1232 load cell ADC.

## Overview

The debug system provides detailed real-time diagnostics of the ADS1232 ADC operation, helping you detect:
- Load cell connection issues (noise/instability)
- ADC communication problems
- Temperature drift
- Electrical interference
- Data quality issues

## Text Commands (via Serial/USB)

Send these text commands via serial terminal:

```
adsd on       # Enable continuous debug output (prints every 1 second)
adsd off      # Disable continuous debug output
adsd info     # Get one-time debug snapshot
```

## Hex/Binary Commands (via USB or BLE)

Send these hex commands for programmatic access. The exact same `0x25`
command set works over BLE (write characteristic `36f5`, notify on `fff4`).

| Command | Hex Bytes | USB behavior | BLE behavior |
|---------|-----------|--------------|--------------|
| Debug OFF       | `03 25 00 XX` | Stops continuous text output     | Stops continuous notify stream |
| Debug ON        | `03 25 01 XX` | Enables continuous text output   | Starts CONTINUOUS notify stream (~10 Hz) |
| Get Info / SINGLE | `03 25 02 XX` | One 41-byte packet on Serial    | One 41-byte notify on `fff4`, auto-clears to OFF |

**Checksum calculation:** XOR of all bytes except the last one.

Example for "Get Info":
```
0x03 ^ 0x25 ^ 0x02 = 0x24
Command: 03 25 02 24
```

> BLE transport eliminates the mechanical drag the USB cable puts on the
> scale during diagnostics, which can otherwise show up as false drift.

## Debug Packet Format

When you send the "Get Info" command (`03 25 02`), the firmware responds with a 41-byte packet:

### Packet Structure

| Bytes | Field | Type | Description |
|-------|-------|------|-------------|
| 0 | Model | uint8 | Always 0x03 |
| 1 | Type | uint8 | Always 0x25 (debug packet) |
| 2-5 | Timestamp | uint32 | millis() when captured |
| 6-9 | Raw Value | int32 | Latest 24-bit ADC reading |
| 10-13 | Smoothed Value | int32 | After averaging filter |
| 14-17 | Tare Offset | int32 | Current tare/zero offset |
| 18-19 | Conversion Time | uint16 | Time in ms Ã— 100 (0.01ms precision) |
| 20-21 | SPS | uint16 | Samples/sec Ã— 100 |
| 22 | Read Index | uint8 | Current position in sample buffer |
| 23 | Samples In Use | uint8 | Number of samples being averaged |
| 24 | Reset Reason | uint8 | Raw `esp_reset_reason()` code captured at boot |
| 25-37 | Reserved | bytes | Zero-filled |
| 38 | Flags | uint8 | Bit 0: Out of range, Bit 1: Timeout |
| 39 | Reserved | uint8 | Always 0 |
| 40 | Checksum | uint8 | XOR of bytes 0-39 |

All multi-byte values are **big-endian** (network byte order).

## Using the Python Tools

Three Python scripts are provided:

### 1. `ads_debug_monitor.py` - USB Serial Communication & Commands

Connects to the scale, sends commands, and displays results.

```bash
# Install pyserial if needed
pip install pyserial

# Single debug info request (default)
python tools/ads_debug_monitor.py /dev/cu.wchusbserial10

# Continuous monitoring every 1 second
python tools/ads_debug_monitor.py /dev/cu.wchusbserial10 --monitor

# Continuous monitoring every 2 seconds
python tools/ads_debug_monitor.py /dev/cu.wchusbserial10 --monitor --interval 2

# Interactive mode (send commands manually)
python tools/ads_debug_monitor.py /dev/cu.wchusbserial10 --interactive

# Enable/disable debug mode
python tools/ads_debug_monitor.py /dev/cu.wchusbserial10 --debug-on
python tools/ads_debug_monitor.py /dev/cu.wchusbserial10 --debug-off
```

### 2. `ads_debug_monitor_ble.py` - BLE Communication & Commands

Same commands as the USB version, but talks to the scale over BLE. Useful
when the USB cable would mechanically interfere with the load cell.

```bash
# Install bleak if needed
pip install bleak

# Auto-scan for "Decent Scale" and request a single packet
python tools/ads_debug_monitor_ble.py

# Specify BLE address directly (skip scan)
python tools/ads_debug_monitor_ble.py --address AA:BB:CC:DD:EE:FF

# Continuous streaming, ~once per second printed, stops on Ctrl+C
python tools/ads_debug_monitor_ble.py --monitor

# Stream for 10 seconds and exit
python tools/ads_debug_monitor_ble.py --monitor --duration 10

# Interactive mode
python tools/ads_debug_monitor_ble.py --interactive
```

### 3. `decode_ads_debug.py` - Packet Decoder

Decodes raw 41-byte packets (used by both monitors above, or standalone).

```bash
# Decode a hex string directly
python tools/decode_ads_debug.py "03250000000100000001..."

# Or import in your own Python code
from decode_ads_debug import decode_ads_debug_packet, print_debug_info
```

### Example Output

```
=== ADS1232 Debug Info ===
Timestamp:      12345 ms
Raw Value:      8388608
Smoothed:       8388610
Tare Offset:    8388500
Conv Time:      100.23 ms
SPS:            9.98
Samples Used:   4
Read Index:     2

Reset Reason:   1

Flags:
  Out of Range: False
  Timeout:      False
==========================
```

## Interpreting the Data

### SPS (Samples Per Second)
Should be around:
- **10 SPS** - Normal mode (default)
- **80 SPS** - Fast mode (if configured)

If SPS is unstable or incorrect, check SCK timing and communication.

### Signal Timeout Flag
If true, the ADC is not responding within the expected time. Check:
- Wiring (DOUT, SCLK, PDWN pins)
- Power supply
- ADC chip health

### Reset Reason
Shows the raw ESP32 reset reason code captured at boot. This helps tell a clean boot from brownout, watchdog, panic, or software restart while collecting debug data.

## Integration Examples

### Python Example (Using the Decoder Library)
```python
import serial
from decode_ads_debug import decode_ads_debug_packet, print_debug_info

ser = serial.Serial('/dev/cu.wchusbserial10', 115200)

# Request debug info
cmd = bytes([0x03, 0x25, 0x02, 0x24])  # Get info command
ser.write(cmd)

# Read response (41 bytes)
response = ser.read(41)
info = decode_ads_debug_packet(response)

if info:
    # Pretty print all info
    print_debug_info(info)
    
    # Or access specific fields
    print(f"Raw ADC value: {info['rawValue']}")
    print(f"Reset reason: {info['resetReason']}")
    
    # Check for issues
    if info['signalTimeout']:
        print("WARNING: ADC signal timeout!")

ser.close()
```

### Python Example (Manual Decoding)
```python
import serial

ser = serial.Serial('/dev/cu.wchusbserial10', 115200)

# Request debug info
cmd = bytes([0x03, 0x25, 0x02, 0x24])
ser.write(cmd)

# Read response (41 bytes)
response = ser.read(41)
if len(response) == 41:
    # Manual decode - raw value (big-endian signed long at bytes 6-9)
    raw_value = int.from_bytes(response[6:10], 'big', signed=True)
    print(f"Raw ADC value: {raw_value}")

ser.close()
```

### Arduino/ESP32 Example
```cpp
// Request debug info
byte cmd[] = {0x03, 0x25, 0x02, 0x24};
Serial.write(cmd, 4);

// Read response
if (Serial.available() >= 41) {
  byte response[41];
  Serial.readBytes(response, 41);
  
  // Decode raw value (big-endian)
  long rawValue = ((long)response[6] << 24) |
                  ((long)response[7] << 16) |
                  ((long)response[8] << 8) |
                  ((long)response[9]);
}
```

## Troubleshooting

### No packet received
1. Check that the device is powered on
2. Verify serial port and baud rate (115200)
3. Try sending text command first: `adsd info`
4. Check that binary data isn't being filtered by terminal

### Invalid checksum
1. Ensure you're reading exactly 41 bytes
2. Check for buffer overflow or partial reads
3. Verify big-endian byte order

### SPS shows 0 or incorrect value
1. ADC may not be initialized properly
2. Check SCLK and DOUT connections
3. Verify PDWN (power down) pin is HIGH

## Callback System (Advanced)

For real-time monitoring in firmware, you can register a callback:

```cpp
void myDebugCallback(const ADS1232DebugInfo& info) {
  // Called on every ADC conversion when debug is enabled
  if (info.signalTimeout) {
    Serial.println("WARNING: ADC signal timeout!");
  }
}

void setup() {
  scale.setDebugCallback(myDebugCallback);
  scale.setDebugEnabled(true);  // Triggers callback on each conversion
}
```

**Note:** Callbacks are called frequently (10-80 times per second), so keep processing minimal.
