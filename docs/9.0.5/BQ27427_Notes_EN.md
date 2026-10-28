# BQ27427 Quick Reference (English)

Sources: TI datasheet SLUSEBSA (`bq27427_datasheet.pdf`) and Technical
Reference Manual SLUUCD5 (`bq27427_trm_sluucd5.pdf`). Raw extracted text in
`extracted/`. All protocol details verified on hardware (see
`tools/probe_9.0.5/`).

## Device

Single-cell Li-ion system-side Impedance Track fuel gauge with integrated
7 mOhm sense resistor. I2C up to 400 kHz, **fixed 7-bit address 0x55**
(write byte 0xAA, read byte 0xAB). Chem profiles: 3230 (4.35 V default),
1202 (4.2 V), 3142 (4.4 V). NORMAL 50 uA / SLEEP 9 uA. Package 9-ball DSBGA (YZF).

## I2C protocol

```
write: S 0xAA CMD DATA... P
read:  S 0xAA CMD Sr 0xAB DATA[7:0] DATA[7:0] N P
```

- Standard commands return 2 bytes, little-endian; address pointer
  auto-increments after each ack; reads above 0x6B are NACKed.
- Standard-command results update at least every 2 s; read-only commands at
  most twice per second. t(BUF) >= 66 us between packets at 400 kHz.
- The I2C engine releases SDA/SCL if held low for 2 s.

## Standard Commands (TRM Table 5-1)

| Command | Code | Unit | Access |
|---|---|---|---|
| Control() | 0x00-0x01 | - | R/W |
| Temperature() | 0x02-0x03 | 0.1 K | R/W |
| Voltage() | 0x04-0x05 | mV | R |
| Flags() | 0x06-0x07 | - | R |
| NominalAvailableCapacity() | 0x08-0x09 | mAh | R |
| FullAvailableCapacity() | 0x0A-0x0B | mAh | R |
| RemainingCapacity() | 0x0C-0x0D | mAh | R |
| FullChargeCapacity() | 0x0E-0x0F | mAh | R |
| AverageCurrent() | 0x10-0x11 | mA | R |
| StandbyCurrent() | 0x12-0x13 | mA | R |
| MaxLoadCurrent() | 0x14-0x15 | mA | R |
| AveragePower() | 0x18-0x19 | mW | R |
| StateOfCharge() | 0x1C-0x1D | % | R |
| InternalTemperature() | 0x1E-0x1F | 0.1 K | R |
| **StateOfHealth()** | **0x20-0x21** | % | R |
| RemainingCapacityUnfiltered() | 0x28-0x29 | mAh | R |
| RemainingCapacityFiltered() | 0x2A-0x2B | mAh | R |
| FullChargeCapacityUnfiltered() | 0x2C-0x2D | mAh | R |
| FullChargeCapacityFiltered() | 0x2E-0x2F | mAh | R |
| StateOfChargeUnfiltered() | 0x30-0x31 | % | R |

Notes: StateOfHealth is at 0x20 (TRM 5.13), not 0x2E; BQ27427 has **no**
CycleCount standard command (tables found online mix in BQ27441 info).
Verified live: SOH @0x20 reads 94%.

## Control() Subcommands (TRM Table 5-2)

Write CONTROL (0x00) + 2-byte little-endian subcommand, re-point to 0x00,
read 2 bytes.

| Subcommand | Value | Sealed | Description |
|---|---|---|---|
| CONTROL_STATUS | 0x0000 | yes | status word (bit13 SS = sealed) |
| DEVICE_TYPE | 0x0001 | yes | **0x0427 for BQ27427** |
| FW_VERSION | 0x0002 | yes | 0x0202 (current ROM) |
| DM_CODE | 0x0004 | yes | data memory config code |
| PREV_MACWRITE | 0x0007 | yes | previous MAC command |
| CHEM_ID | 0x0008 | yes | hex-nibble chem id (0x1202 = "1202") |
| BAT_INSERT / BAT_REMOVE | 0x000C/0x000D | yes | force BAT_DET flag |
| SET_CFGUPDATE | 0x0013 | no | enter CONFIG UPDATE mode |
| SMOOTH_SYNC | 0x0019 | yes | sync smoothed capacity |
| SHUTDOWN_ENABLE / SHUTDOWN | 0x001B/0x001C | no | shutdown mode |
| SEALED | 0x0020 | no | enter sealed mode |
| PULSE_SOC_INT | 0x0023 | yes | 1 ms GPOUT pulse |
| CHEM_A/B/C | 0x0030-0x0032 | no | switch chem 3230/1202/3142 |
| RESET | 0x0041 | no | full device reset |
| SOFT_RESET | 0x0042 | no | exit CONFIG UPDATE, resume gauging |

UNSEAL key: 0x8000, written twice.

## Chem switch to 1202 (4.2 V cell, verified)

```
1. (if sealed) UNSEAL 0x8000 x2
2. SET_CFGUPDATE 0x0013
3. wait 1 s
4. CHEM_B 0x0031
5. wait 100 ms
6. SOFT_RESET 0x0042
7. wait 2 s
8. verify CHEM_ID == 0x1202
```

Persists in NVM.

## Data timing

- Voltage/current: immediate.
- SOC: valid ~2 s after reset or chem switch.
- FCC / SOH: converge after ~1-2 full charge/discharge cycles (new cell or
  right after a chem switch).
