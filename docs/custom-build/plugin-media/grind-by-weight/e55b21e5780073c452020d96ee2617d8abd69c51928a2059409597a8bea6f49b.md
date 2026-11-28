# HDS Grind by weight

HDS support for using a Tasmota smart plug as a grinder power switch.

Match the plug firmware to the HDS firmware ref you selected in the custom build configurator: `main` uses BLE, while tags through `v3.1.17` use WiFi/TCP. The two plug firmwares are not interchangeable.

## What It Does

- Lets the scale switch the grinder plug on and off automatically
- After a new plug connection, starts only after you tare the empty cup and it becomes stable
- Stops the grinder when the dose reaches the target
- Waits for cup removal before preparing the next dose
- Learns a better safety value from normal shots over time
- Keeps the plug off if the connection is lost while grinding
- Keeps normal weighing responsive even when the plug is offline

The grinder itself still needs to be ready to run. This feature controls power to the plug, not the physical grinder switch.

## Setup

### Tagged Firmware: WiFi

1. Install [Tasmota Auto Doser v1.1.0](https://github.com/decentespresso/tasmota-auto-doser/releases/tag/v1.1.0) for your plug model. Connect the plug to a trusted WiFi network using the [plug setup guide](https://github.com/decentespresso/tasmota-auto-doser/blob/v1.1.0/docs/plug-setup.md).
2. Build the chosen HDS tag with the `Grind by weight` plugin, flash it to the scale, and connect the scale to the same WiFi network. The network must allow local connections and mDNS discovery between the devices.
3. Open `Grind by weight` in the HDS OLED menu, enable it, and use `Select Plug` to choose the plug by its factory MAC address.
4. Adjust the dosing settings as needed and leave the menu with `Back`.

### Main Firmware: BLE

1. Back up the plug settings and keep both previous firmware images for rollback. Install the [matching BLE grinder firmware](https://github.com/decentespresso/tasmota-auto-doser/pull/20) for your plug model. The plug's WiFi remains available for settings and updates; this grinder connection does not need WiFi on either device.
2. Build `main` with the `Grind by weight` plugin and flash it to the HDS scale. Power on with the circle button to enable BLE.
3. Open `Grind by weight` in the HDS OLED menu, enable it, and use `Select Plug` to choose the plug by its factory MAC address.
4. Adjust the dosing settings as needed and leave the menu with `Back`.

If you already selected this plug on the WiFi firmware, the scale keeps its factory MAC and your dosing settings. There is no WiFi fallback. Decaid can stay connected to the scale over BLE while the scale controls the plug. To use the scale's tare button while Decaid is connected, enable `BLE Buttons` in the scale's `Connections` menu.

## First Safe Test

Disconnect the grinder. Use a lamp so you can see whether the outlet is energized, not just whether the software reports ON or OFF.

1. Complete the setup for your firmware ref and leave the menu.
2. Confirm that the display reaches `Tare to arm`.
3. Put the empty dosing cup in its normal position and tare the scale.
4. Wait for `Ready` and confirm that the plug relay switches on.
5. Add weight gradually until the cutoff is reached and confirm that the plug switches off.
6. Remove the weight and return the empty cup. After stable zero, the display should return to `Ready`.

On `main`, repeat with Windows Decaid connected to the physical scale over BLE and WiFi unavailable to both devices. Confirm Decaid reports weight during dosing and automatic plug reconnection, and remains connected while the scale scans for plugs. This test does not qualify a tablet with a real DE1 connected.

## Settings

From the normal weight view, hold both buttons for 500 ms to open the `Grind by weight` menu when the feature is enabled. Use `Target g` there for quick target changes.

Defaults:

```text
Grind by weight: disabled
Target: 15.0 g
Safety: 2.0 g
Zero range: -1.0 g to 1.0 g
Zero hold: 1000 ms
Target tolerance: 0.5 g
```

Tagged firmware keeps WiFi on at boot when the grinder is enabled; `main` does not change your WiFi setting. Entering the grinder menu while the plug is armed sends OFF first. The plug connection stays open where possible, and leaving the menu rearms only after stable zero.

The cutoff threshold is:

```text
target grams - safety grams
```

Latency compensation is not used. Adaptive safety is the only early-stop compensation.

Example with the defaults:

```text
target: 15.0 g
safety: 2.0 g
cutoff: 13.0 g
```

The earlier cutoff allows for grounds that continue falling after power is removed. Adaptive learning adjusts safety from valid completed doses; it does not change the target.

## Normal Use

1. Enable Grind by weight and select a plug.
2. Put the empty dosing cup on the scale.
3. On the first plug connection after Grind by weight starts, the display shows `Tare to arm`.
4. Tare the scale.
5. Wait until the cup is stable inside the zero range.
6. The display shows `Ready` and the scale sends `ON` to the plug.
7. Start the grinder physically if needed.
8. The display changes to `Grinding` after a rising dose is detected.
9. The scale sends fast OFF when cutoff is reached.
10. Remove the filled cup.
11. Put the empty cup back.
12. The scale rearms after stable zero hold without requiring another tare.

After a reconnect, the existing tare baseline remains valid. The relay stays off; remove and return the cup to resume without another tare. Automatic startup, charging-wake, and ADC-recovery tares do not arm the grinder.

Once the relay is ON, a tare from Decaid or the scale resets the cutoff baseline while leaving the relay powered. Do not tare during a dose. If the cup or setup changes, turn the grinder off before taring again.

After `Ready` appears, only ground coffee should add weight. Slowly placing another object can look like a valid grind because the scale only sees weight changes.

## Display Status

| Display | Meaning | What to do |
| --- | --- | --- |
| `WiFi wait` (tags) | Scale is waiting for WiFi | Check the scale's saved network |
| `BLE off` (`main`) | Scale started without BLE | Restart the scale with the circle button |
| `Plug wait` | Selected plug is not reachable | Power the plug or use `Select Plug` |
| `Tare to arm` | Plug is connected but safely OFF | Put down the empty cup and tare |
| `Zero wait` | Waiting for a stable zero reading | Leave the empty cup untouched |
| `Ready` | Plug relay is ON and dosing can begin | Start the grinder physically |
| `Grinding` | A plausible rising dose was detected | Let the dose run |
| `Stopping` | OFF was sent and acknowledgement is pending | Wait briefly |
| `Remove cup` | Dose ended | Remove the filled cup |
| `Await zero` | Waiting for the empty cup to return | Return the empty cup and leave it stable |
| `Tare cup` | Weight rose too quickly to be a plausible grind | Return to zero or tare the changed setup |
| `Busy` | Another scale/client owns the plug connection | Disconnect the other client or restart the plug |
| `Error` | Fail-safe stopped Grind by weight | Check the plug and reconnect Grind by weight |

`Grinding` is a display indication, not the cutoff authority. It appears after at least 750 ms, 1.0 g rise, three positive steps, and an average rise no faster than 6 g/s.

## Cutoff Protection

OFF is blocked until all of these are true:

- tare is not pending
- one user-requested tare completed during the current grinder session
- weight has left zero range
- 1500 ms passed since leaving zero range
- weight is at or above `target - safety`
- the selected plug connection is still valid

Grind confirmation changes the display from `Ready` to `Grinding`. If cutoff weight is reached at an impossible average rate above 6 g/s, the scale treats it as cup/setup mass and does not send OFF. This block remains until tare or return to zero, so a stationary cup cannot later become a valid grind merely because time passes.

`Target g` accepts 10.0 to 200.0 g. `Safety g` accepts 0.0 to the smaller of 10.0 g or `target - 5.0 g`, keeping cutoff at 5.0 g or higher.

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

### Tagged Firmware: WiFi

The scale discovers `_grinderplug._tcp.local` over mDNS and connects on TCP port `31980`. It stores the plug MAC and caches its hostname and last IP for lookup. See the [TCP protocol guide](https://github.com/decentespresso/tasmota-auto-doser/blob/v1.1.0/docs/grinder-tcp.md).

### Main Firmware: BLE

The plug advertises a dedicated BLE service. Manufacturer data starts with the little-endian company ID `FF FF`, followed by six bytes of its factory WiFi STA MAC in display order. HDS uses the MAC as an identity label and does not use the plug's current BLE address as its saved identity.

```text
Service UUID:        6f735400-6d61-4d8e-bb2b-f5e23b9d7801
Command write UUID:  6f735401-6d61-4d8e-bb2b-f5e23b9d7801
Reply notify UUID:   6f735402-6d61-4d8e-bb2b-f5e23b9d7801
```

Each write or notification contains a one-byte request ID and 1 to 19 ASCII bytes. Requests use IDs 1 through 255; every fragment of a message uses the same ID and replies echo it. ID 0 is reserved for unsolicited `BUSY` or `ERR`. Lines end in LF except for the standalone fast OFF command `!`.

The scale subscribes for replies, sends `HELLO <scale_mac>`, and checks the returned MAC against the saved selection. After that it sends `PING` every 500 ms and uses `!` at cutoff. The plug supports `STATE`, `ON`, `OFF`, and `BYE` with the same relay semantics as the WiFi version.

The plug starts with its relay off. It turns off if the owning connection drops or no valid heartbeat arrives for two seconds. A second client receives `BUSY` without affecting the owning connection.

## Troubleshooting

### Select Plug Finds Nothing

On tags, check that both devices are on the same WiFi network and that the plug advertises `_grinderplug._tcp.local` with its MAC and protocol version `1`. On `main`, check that the plug has the BLE firmware and is within range. A BLE scanner should show the grinder service UUID and the factory MAC in manufacturer data.

### Scale Shows Plug Wait

The selected plug cannot currently be reached.

Check:

- plug is powered
- the saved MAC in `Select Plug` matches the intended plug
- on tags, the plug is on the same network and listens on TCP port `31980`
- on `main`, the plug is within BLE range

### Scale Shows Busy

Another scale or client is already connected to the plug. Disconnect the other client or restart the plug.

### Scale Shows Wrong Mac

The plug replied with a MAC different from the selected identity. Check the firmware on the plug and verify the intended MAC before selecting it again.

### Scale Shows an Error

The scale entered the Grind by weight fail-safe state.

Common causes:

- plug connection lost while grinding
- malformed plug response
- wrong plug MAC
- plug returned `ERR`
- `ON` timed out
- `OFF` timed out

The plug should turn OFF when the owning connection drops or its heartbeat expires.

### Scale Shows Tare To Arm

The plug connection is valid, but the grinder remains OFF until a user-requested tare completes. Put the empty basket on the scale and press tare. Startup and recovery tares do not clear this prompt.

### Plug Is Offline And Weighing Must Stay Responsive

During normal weighing, the scale looks for the saved plug. Use `Select Plug` for a fresh mDNS lookup on tags or to list nearby BLE plugs on `main`.

### The Scale Does Not Rearm

Rearming requires:

- filled cup removal detected
- empty cup returned
- weight stable inside zero range for the zero hold time

Default zero hold is 1000 ms.

## Serial Debug

Grind by weight debug logs are printed on USB serial. The internal prefix remains:

```text
[grinder] connect
[grinder] tx HELLO
[grinder] rx OK
[grinder] cutoff
[grinder] safety learn
[grinder] error
```

## Security Notes

Neither transport authenticates the plug or the controller. On tags, keep both devices on a trusted local WiFi network. On `main`, the first BLE client to connect owns the plug until it disconnects or times out. There is no PIN or bond. A device within Bluetooth range can claim an idle plug and turn its relay on, even without access to your WiFi. Use the BLE build only where you trust nearby devices.
