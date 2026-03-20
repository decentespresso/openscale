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

## Hex/Binary Commands (via USB)

Send these hex commands for programmatic access:

| Command | Hex Bytes | Description |
|---------|-----------|-------------|
| Debug ON | `03 25 01 XX` | Enable debug mode (XX = checksum) |
| Debug OFF | `03 25 00 XX` | Disable debug mode (XX = checksum) |
| Get Info | `03 25 02 XX` | Request debug packet (XX = checksum) |

**Checksum calculation:** XOR of all bytes except the last one.

Example for "Get Info":
```
0x03 ^ 0x25 ^ 0x02 = 0x24
Command: 03 25 02 24
```

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
| 18-19 | Conversion Time | uint16 | Time in ms × 100 (0.01ms precision) |
| 20-21 | SPS | uint16 | Samples/sec × 100 |
| 22 | Read Index | uint8 | Current position in sample buffer |
| 23 | Samples In Use | uint8 | Number of samples being averaged |
| 24-27 | Data Min | int32 | Minimum value in dataset |
| 28-31 | Data Max | int32 | Maximum value in dataset |
| 32-35 | Data Avg | int32 | Average of dataset |
| 36-37 | Std Dev | uint16 | Standard deviation × 10 (0.1 precision) |
| 38 | Flags | uint8 | Bit 0: Out of range, Bit 1: Timeout, Bit 2: Tare active |
| 39 | Tare Times | uint8 | Tare sample counter |
| 40 | Checksum | uint8 | XOR of bytes 0-39 |

All multi-byte values are **big-endian** (network byte order).

## Using the Python Tools

Two Python scripts are provided:

### 1. `ads_debug_monitor.py` - Serial Communication & Commands

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

### 2. `decode_ads_debug.py` - Packet Decoder

Decodes raw hex packets (used by ads_debug_monitor.py or standalone).

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

Dataset Stats:
  Min:          8388605
  Max:          8388615
  Avg:          8388610
  Range:        10
  Std Dev:      3.2 (lower = more stable)

Flags:
  Out of Range: False
  Timeout:      False
  Tare Active:  False
==========================
```

## Interpreting the Data

### Standard Deviation (Noise Indicator)
This is the most important metric for detecting issues:

- **< 10**: Excellent stability - load cell and ADC working perfectly
- **10-50**: Good - normal operation
- **50-200**: Fair - some noise, check connections
- **> 200**: Problems - likely connection, grounding, or EMI issues

### SPS (Samples Per Second)
Should be around:
- **10 SPS** - Normal mode (default)
- **80 SPS** - Fast mode (if configured)

If SPS is unstable or incorrect, check SCK timing and communication.

### Data Range (Max - Min)
Shows variation in the sample buffer:
- Small range (< 20) = stable
- Large range = noisy or changing weight

### Signal Timeout Flag
If true, the ADC is not responding within the expected time. Check:
- Wiring (DOUT, SCLK, PDWN pins)
- Power supply
- ADC chip health

### Tare Progress
When tare is active, `tareTimes` shows how many samples have been collected for the tare operation.

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
    print(f"Std Dev: {info['dataStdDev']}")
    
    # Check for issues
    if info['dataStdDev'] > 100:
        print("WARNING: High noise detected!")

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

### Constant high standard deviation
1. Check load cell wiring (excitation +/-, signal +/-)
2. Verify proper grounding
3. Check for EMI sources nearby (motors, switching supplies)
4. Ensure stable power supply to ADC
5. Check for mechanical vibration

### SPS shows 0 or incorrect value
1. ADC may not be initialized properly
2. Check SCLK and DOUT connections
3. Verify PDWN (power down) pin is HIGH

## Callback System (Advanced)

For real-time monitoring in firmware, you can register a callback:

```cpp
void myDebugCallback(const ADS1232DebugInfo& info) {
  // Called on every ADC conversion when debug is enabled
  if (info.dataStdDev > 100) {
    Serial.println("WARNING: High noise detected!");
  }
}

void setup() {
  scale.setDebugCallback(myDebugCallback);
  scale.setDebugEnabled(true);  // Triggers callback on each conversion
}
```

**Note:** Callbacks are called frequently (10-80 times per second), so keep processing minimal.
