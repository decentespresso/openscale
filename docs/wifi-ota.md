# HDS WiFi OTA Updates

Use the scale's WiFi connection to install a signed HDS firmware release without a USB cable.

## Requirements

- The installed firmware must include the `WiFi Update` menu.
- The scale must have saved credentials for a WiFi network with internet access.
- Keep the scale on reliable USB power until the update finishes.
- Production WiFi OTA releases start at `v3.1.13`.

Older firmware without the `WiFi Update` menu needs one USB update before it can use WiFi OTA.

To open the HDS setup menu, turn on the scale while holding both the circle and square buttons.

## First-Time WiFi Setup

Skip this section if the scale already connects to your WiFi network.

1. Open `WiFi Settings` on the scale and select `WiFi On`.
2. Restart the scale.
3. Connect a phone or computer to the `DecentScale` access point with password `12345678`.
4. Open [http://hds.local](http://hds.local) or `http://192.168.1.1`.
5. Enter the home WiFi credentials and wait for the scale to restart.

## Install An Update

1. Keep the scale connected to USB power.
2. Open the HDS setup menu.
3. Open `WiFi Settings`.
4. Select `WiFi Update`.
5. Wait while the scale connects to WiFi and checks the signed release catalog.
6. On `Install version`, press the circle button to move through the available versions.
7. Press the square button to select a version.
8. Check the displayed current and selected versions.
9. Hold the square button until the download starts.
10. Leave the scale powered until it reports completion and returns to the normal weight display.

Hold the circle button to cancel from the version list. The selection and confirmation screens also cancel if left idle.

The catalog may include a compatible older release. Check the version before confirming if you do not want to downgrade.

## What To Expect

The scale downloads only releases that match its hardware and firmware rules. It verifies the signed manifest, HTTPS connection, download URL, file size, and SHA-256 hash before installing anything.

Production releases update both firmware and the built-in web apps:

1. The scale installs the firmware and restarts.
2. The new firmware shows `Updating web UI` and installs the filesystem.
3. The scale verifies the filesystem and restarts again.
4. The normal weight display returns when the update is complete.

Two restarts are normal. Do not disconnect power while the display shows `Firmware`, `Updating web UI`, or `LittleFS` progress.

If no compatible update or downgrade is available, the scale shows `Newest stable` with the installed version.

## Retry A Failed Update

If the firmware download fails, the scale keeps running the installed firmware. Check the message on the display, restore WiFi or internet access, and start `WiFi Update` again.

If the firmware installed but the web UI update failed, the scale keeps the pending web UI update. Restart the scale with WiFi available, or open `WiFi Update` again. The scale retries the pending filesystem before offering another firmware version.

## Display Messages

| Display | Meaning | What to do |
| --- | --- | --- |
| `No saved WiFi` | The scale has no saved network credentials | Complete first-time WiFi setup |
| `WiFi failed` | The saved network did not connect in time | Check the access point and try again |
| `Clock failed` | The scale could not establish the time required for TLS | Check internet access and try again |
| `Signature failed` | The release catalog or signature could not be downloaded or verified | Try again later; do not bypass this check |
| `Manifest invalid` | No valid release catalog could be parsed | Try again later |
| `Download failed` | A release file could not be downloaded | Check internet access and try again |
| `Size mismatch` or `Hash mismatch` | The downloaded file did not match the signed manifest | Try again later; do not install files manually from that download |
| `Update cancelled` | A button hold or screen timeout cancelled selection | Start `WiFi Update` again |
| `FS verify failed` | The web UI filesystem did not verify after writing | Keep power and WiFi available, then retry |
| `Newest stable` | No other compatible stable version is available | No action is needed |

## Troubleshooting

### WiFi Update Is Missing

Install a firmware version that includes WiFi OTA over USB first. WiFi OTA cannot be added to older firmware without flashing that firmware once.

### The Scale Cannot Connect

Open `WiFi Status` and confirm the saved network. If the network name or password changed, use `Reset WiFi`, restart the scale, and repeat first-time setup.

The scale must reach GitHub over HTTPS. A local-only network, captive portal, DNS block, or firewall can prevent the update.

### The Web Interface Looks Outdated

Wait for the second restart. The firmware restart happens before the built-in web apps are updated. After completion, reload `hds.local`; the server asks browsers not to reuse stale app files.

### The Scale Restarts Into The Previous Firmware

The new firmware did not pass its startup validation and the device rolled back. Keep the scale powered and update over USB before retrying WiFi OTA.

## Security

HDS accepts only manifests signed by the release key embedded in the firmware. It also validates GitHub HTTPS certificates and release asset hashes. Treat `Signature failed`, `Manifest invalid`, and hash errors as failed updates. Do not work around them with an unsigned file or an insecure connection.
