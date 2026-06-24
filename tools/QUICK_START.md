# ADS Debug Quick Start Guide

## Installation

```bash
pip install pyserial
```

## Most Common Commands

### Get Debug Info Once
```bash
python tools/ads_debug_monitor.py /dev/cu.wchusbserial10
```

### Continuous Monitoring
```bash
python tools/ads_debug_monitor.py /dev/cu.wchusbserial10 --monitor --interval 2
```

### Interactive Mode
```bash
python tools/ads_debug_monitor.py /dev/cu.wchusbserial10 --interactive
```

Interactive commands:
- `info` - Get debug snapshot
- `on` - Enable debug mode
- `off` - Disable debug mode
- `monitor` - Start continuous monitoring
- `quit` - Exit

## Via Serial Terminal

Send these text commands:
```
adsd info     # Get one-time debug info
adsd on       # Enable continuous debug
adsd off      # Disable debug
```

## Via Hex Commands

Send these binary commands:
```
03 25 02 24   # Get debug info (checksum = 0x24)
03 25 01 27   # Enable debug (checksum = 0x27)
03 25 00 26   # Disable debug (checksum = 0x26)
```

## What to Look For

| Metric | Good | Warning | Problem |
|--------|------|---------|---------|
| **SPS** | ~10 or ~80 | Unstable | 0 or very high |
| **Signal Timeout** | False | - | True |

### Quick Diagnostics

**Signal Timeout?**
1. Check DOUT, SCLK, PDWN connections
2. Verify power supply
3. Check ADC chip health

**SPS is 0 or unstable?**
1. ADC not initialized properly
2. Check clock signals
3. Verify power-down pin is HIGH

## Example Session

```bash
$ python tools/ads_debug_monitor.py /dev/cu.wchusbserial10 --interactive

Opening /dev/cu.wchusbserial10 at 115200 baud...
Connected.

=== Interactive Mode ===
Commands:
  on      - Enable debug mode
  off     - Disable debug mode
  info    - Get debug info
  monitor - Start continuous monitoring
  quit    - Exit

debug> info
Sent: GET INFO (03 25 02 24)

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

debug> quit

Exiting...
Serial port closed.
```

## Integration in Your Code

```python
import serial
from decode_ads_debug import decode_ads_debug_packet, print_debug_info

ser = serial.Serial('/dev/cu.wchusbserial10', 115200)

# Send get info command
cmd = bytes([0x03, 0x25, 0x02, 0x24])
ser.write(cmd)

# Read response (41 bytes)
response = ser.read(41)
info = decode_ads_debug_packet(response)

if info:
    print_debug_info(info)
    
    # Access individual fields
    if info['signalTimeout']:
        print("WARNING: ADC signal timeout!")

ser.close()
```

## Troubleshooting

**No response from scale?**
- Check serial port name
- Verify baud rate (115200)
- Try text command first: `adsd info`

**"Checksum mismatch" error?**
- Data corruption on serial line
- Try reducing baud rate
- Check cable quality

**Script not found?**
- Make sure you're in the repo root
- Use full path: `python /path/to/tools/ads_debug_monitor.py ...`

## Need More Help?

See full documentation: [README_ADS_DEBUG.md](README_ADS_DEBUG.md)
