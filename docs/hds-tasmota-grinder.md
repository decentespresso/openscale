# HDS Tasmota Grinder Mode

HDS support for using a Tasmota smart plug as a grinder power switch.

Plug-side firmware lives at:

https://github.com/decentespresso/tasmota-auto-doser

This is a trusted-local-WLAN feature. The plug MAC is used as an identity label, not as authentication.

## What It Does

- Lets the scale switch the grinder plug on and off automatically
- Starts only after the empty cup is stable on the scale
- Stops the grinder when the dose reaches the target
- Waits for cup removal before preparing the next dose
- Learns a better safety value from normal shots over time
- Keeps the plug off if the connection is lost while grinding
- Keeps normal weighing responsive even when the plug is offline

The grinder itself still needs to be ready to run. This feature controls power to the plug, not the physical grinder switch.

## Plug Setup

Install the linked plug firmware on the Tasmota plug, then connect the plug to the same WiFi network as the scale.

## Scale Setup

1. Flash firmware with grinder support to the HDS scale.
2. Connect the scale to WiFi.
3. Open the HDS OLED menu.
4. Open `Grinder Plug`.
5. Select `Grinder On`.
6. Select `Select Plug`.
7. Choose the plug MAC address.
8. Adjust the grinder settings if needed.
9. Leave the menu with `Back`.

The selected MAC is the saved identity. Hostname and IP are cached only to find the plug faster later.

## Settings

From the normal weight view, hold both buttons for 500 ms to open the `Grinder Plug` menu when grinder mode is on. Use `Target g` there for quick target changes.

Defaults:

```text
Grinder: Off
Target: 15.0 g
Safety: 2.0 g
Zero range: -1.0 g to 1.0 g
Zero hold: 1000 ms
Target tolerance: 0.5 g
```

`Grinder On` forces WiFi on boot.

The cutoff threshold is:

```text
target grams - safety grams
```

Latency compensation is not used. Adaptive safety is the only early-stop compensation.

## Normal Use

1. Turn grinder mode on and select a plug.
2. Put the empty dosing cup on the scale.
3. Tare the scale.
4. Wait until the cup is stable inside the zero range.
5. The scale sends `ON` to the plug.
6. Start the grinder physically if needed.
7. The scale sends fast OFF when cutoff is reached.
8. Remove the filled cup.
9. Put the empty cup back.
10. The scale rearms after stable zero hold.

## Cutoff Protection

OFF is blocked until all of these are true:

- tare is not pending
- weight has left zero range
- 1500 ms passed since leaving zero range
- a real grind pattern was confirmed
- weight is at or above `target - safety`
- the selected plug connection is still valid

If a cup or other setup mass is placed on the scale before tare, the scale shows `tare cup` and blocks cutoff until the user tares or the weight returns to zero range.

## Adaptive Safety

After a valid grind, the scale compares final weight to target and adjusts safety for later shots.

Example:

```text
target: 15.0 g
final: 15.3 g
result: safety increases for future shots
```

The adaptive value averages the last 3 valid recommendations.

Learning is skipped when the shot does not look like a normal grind:

- final weight did not become stable
- the cup was removed too early
- weight dropped after OFF
- the grind stalled below target
- average grind rate was too high
- final result was too far from target

Adaptive changes are saved before deep sleep instead of on every loop.

## Technical Details

The plug advertises itself with mDNS:

```text
service: _grinderplug._tcp.local
port: 31980
TXT mac=<plug_mac>
TXT proto=1
TXT model=<optional model name>
```

The scale stores the selected plug MAC as the identity. Hostname and last IP are only cached lookup hints.

The scale keeps one TCP connection open while active. It sends `HELLO <scale_mac>` and only accepts the plug if the returned MAC matches the selected MAC.

After validation, cutoff uses the fast OFF command `!`. The scale also sends PING heartbeats while connected.

The plug relay must default to OFF on boot. If the active TCP connection drops, the plug must also turn the relay OFF.

Each plug accepts one active scale connection. A second client receives `BUSY` and is closed.

## Troubleshooting

### Select Plug Finds Nothing

Check mDNS from another machine:

```powershell
dns-sd -B _grinderplug._tcp local
dns-sd -L "INSTANCE NAME HERE" _grinderplug._tcp local
```

Expected result:

```text
port=31980
mac=<plug_mac>
proto=1
model=<optional model name>
```

If `_http._tcp` appears but `_grinderplug._tcp` does not, the plug service advertisement is wrong.

### Scale Shows Plug Wait

The selected plug cannot currently be reached.

Check:

- plug is powered
- plug is on the same WLAN
- plug TCP service is listening on port 31980
- saved hostname still resolves
- saved IP still belongs to the selected plug

If the plug IP changed and hostname lookup does not recover it, run `Select Plug` again.

### Scale Shows Busy

Another scale or client is already connected to the plug. Disconnect the other client or restart the plug.

### Scale Shows Wrong Mac

The TCP server answered with a MAC different from the selected plug MAC. This usually means the cached IP points to another device. Run `Select Plug` again.

### Scale Shows Grinder Error

The scale entered fail-safe grinder error.

Common causes:

- plug connection lost while grinding
- malformed TCP response
- wrong plug MAC
- plug returned `ERR`
- `ON` timed out
- `OFF` timed out

The plug is expected to turn OFF automatically when the active TCP connection drops.

### Plug Is Offline And Weighing Must Stay Responsive

Normal weighing does not run full blocking service discovery in the background. Runtime lookup only tries the saved IP and saved hostname with short timeouts. Full mDNS discovery happens in manual `Select Plug`.

### The Scale Does Not Rearm

Rearming requires:

- filled cup removal detected
- empty cup returned
- weight stable inside zero range for the zero hold time

Default zero hold is 1000 ms.

## Serial Debug

Grinder debug logs are printed on USB serial. Useful prefixes:

```text
[grinder] connect
[grinder] tx HELLO
[grinder] rx OK
[grinder] cutoff
[grinder] safety learn
[grinder] error
```

## Security Notes

This protocol has no cryptographic authentication. Any host on the trusted WLAN that can reach the plug or scale network services may interact with them. Do not expose the scale or plug to untrusted networks.
