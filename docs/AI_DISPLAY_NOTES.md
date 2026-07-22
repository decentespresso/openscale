# AI Display Notes

Read this when changing the OLED controller, U8g2 rendering, setup menus, weighing-screen layout, icons, display rotation, or a user-configurable display option. Skip it for changes limited to scale filtering, transport payloads, or web-app UI.

## Source Of Truth

| Concern | Primary source |
| --- | --- |
| Board profile, OLED controller, bus, pins, margins | `include/config.h` |
| U8g2 object, font aliases, alignment macros, XBM icons | `include/display.h` |
| Setup-menu model, actions, navigation, calibration UI | `include/menu.h` |
| Main weighing frame and normal button dispatch | `src/hds.ino` |
| Runtime UI state and refresh interval | `include/parameter.h` |
| Normal-mode tare/timer press classification | `include/finger_detection.h` |
| Persistent UI settings | `include/storage.h`, `docs/AI_STORAGE_NOTES.md` |

The firmware is a unity-style build. `include/display.h` and `include/menu.h` contain definitions, not only declarations. Do not include them from a new translation unit without checking for duplicate definitions.

## Current Hardware Profile

The default `V8_1` configuration selects hardware SPI and an SH1106-family 128x64 constructor. The code also contains SSD1306, SSD1312, SH1106, and SH1116 paths.

A request that calls the panel “SSD1306” may be referring generically to the OLED. Do not change the constructor, bus, or pins for a layout-only request. Confirm the target PCB and controller before changing display hardware macros.

The software-SPI constructor branches in `include/display.h` are not fully mutually exclusive. If that path is touched, first ensure preprocessing can instantiate exactly one global `u8g2` object.

## Rendering Ownership

The constructors use U8g2 page-buffer mode. A complete frame owns this pattern:

```cpp
u8g2.firstPage();
do {
  // Draw the complete frame.
} while (u8g2.nextPage());
```

The page body can execute more than once. It must be deterministic.

Do not perform these operations inside a page body:

- update application state
- write NVS
- start or stop timers
- delay or wait for input
- perform network, filesystem, ADC, or power work
- allocate large transient objects

Calculate a state snapshot before `firstPage()`, then render it inside the loop.

Do not nest page loops. A helper called from `updateOled()` draws into the existing frame and must not call `firstPage()` or `nextPage()`.

All `u8g2.*`, SPI, I2C, and display-power operations remain on the main loop task. AsyncTCP and WebSocket callbacks may update protected state or queue work, but must not render.

## Main Weighing Frame

Normal display flow:

```text
loop()
  -> pureScale()
  -> updateOled()
       -> drawWeight(f_displayedValue)
       -> drawTime()
       -> drawBattery()
       -> drawButton()
       -> drawBle()
       -> drawHeartBeat()
       -> drawGrinder()
       -> drawTare()
       -> drawShutdownFail()
       -> drawAbout()
       -> drawDebug()
```

`i_oled_print_interval` is currently 100 ms. Keep the 10 Hz render path lightweight.

The visible weight source is `f_displayedValue`, after the scale-processing pipeline. A visual-only request must not replace it with raw ADC data or alter drift, tracking, stable-output, or tare logic.

`updateOled()` draws weight and time first, then icons and overlays. Later functions can overwrite or invert earlier pixels. Place passive icons early and full-screen messages late.

Other states own separate screens:

- setup menu
- calibration
- charging
- OTA
- ADC recovery
- debug and about overlays

Put state-specific rendering in the state that owns it rather than adding every screen to `drawWeight()`.

## Text And Layout Rules

`drawStr()` and `drawUTF8()` use a baseline `y`, not a top coordinate.

Set the font before measuring text. `AC()` and `AR()` call `getUTF8Width()` with the current font.

```cpp
u8g2.setFont(FONT_S);
int x = AC(label);
u8g2.drawStr(x, y, label);
```

Use `drawStr()` for ASCII and `drawUTF8()` only when both the text and selected font support the needed glyphs.

Prefer `LCDWidth` and `LCDHeight` over new hard-coded display dimensions. Existing fixed coordinates may remain when they deliberately target the 128x64 layout.

Use fixed-size `char` buffers and `snprintf()` in the hot path. Do not reintroduce temporary `String` objects for timer or weight formatting.

Every font referenced by the firmware is linked into the image. Build after changing font aliases and check firmware size.

## Weight And Timer Layout

`drawWeight()` currently:

1. detects positive overweight using `OVER_WEIGHT`
2. rounds to tenths
3. preserves the sign for values between `-1.0` and `0.0`
4. measures the integer, decimal point, decimal digit, and unit
5. centers the complete visual group
6. chooses a baseline based on timer visibility and `b_timeOnTop`

`drawTime()` hides the timer when the elapsed text is `0s`.

The existing persistent `Time On Top` menu option controls whether time or weight occupies the upper position. Reuse that state rather than adding a duplicate timer-position option.

When changing weight format or fonts, test at least:

```text
-1000.0, -999.9, -0.1, 0.0, 0.1, 9.9, 10.0,
99.9, 100.0, 999.9, 1000.0, OVER_WEIGHT
```

Also test timer stopped, timer running, and both `b_timeOnTop` values.

Showing two decimal places is not only a format-string change because the existing renderer manually separates one decimal digit. Rework the complete measurement and centering path, and do not imply measurement accuracy the sensor pipeline does not provide.

If hiding or changing the `g` suffix, update the width calculation as well as the draw call.

## Icons And Overlays

XBM data belongs in `include/display.h`. Add a small `draw...()` helper in `src/hds.ino` and call it from `updateOled()` in the required layer order.

Occupied regions include:

- top corners: pressed-button icons
- bottom left: BLE and WiFi
- bottom right: battery
- lower center: grinder status
- bottom center: tare marker
- center and upper/lower areas: weight and timer

Check every persistent icon against both rotations and all connection states.

For a full-screen transient overlay:

1. gate it with runtime state and `millis()`
2. clear the frame area with draw color 0
3. restore draw color 1 for text
4. call it late in `updateOled()`
5. do not use `delay()` in the normal weighing frame

Restore the draw color and font assumptions required by later helpers.

## Setup Menu Model

The menu item type is:

```cpp
struct Menu {
  const char *name;
  void (*action)();
  Menu *subMenu;
  Menu *parentMenu;
};
```

Circle advances the current selection. Square selects it.

Display order comes from pointer arrays such as `mainMenu[]`, not declaration order.

Adding a submenu requires all of these:

1. parent and child `Menu` declarations
2. child pointer array
3. parent pointer in `mainMenu[]` or another reachable array
4. `linkSubmenus()` registration
5. `selectMenu()` branch that selects the array and sets its size

Missing one can produce an item that is visible but not enterable, has the wrong item count, or cannot be reached.

`showMenu()` currently uses four rows per page, a `>` marker, and a top-right page count. Long labels do not wrap. After changing font, row spacing, or labels, check every menu and the page indicator.

The current Back implementation returns to `mainMenu`; `parentMenu` does not provide generic arbitrary-depth traversal. Do not add a third menu level without first introducing a real parent-array stack or equivalent navigation state.

Menu actions may show temporary feedback through:

```cpp
actionMessage
actionMessage2
t_actionMessage
t_actionMessageDelay
```

Preserve `t_menuExitTime` protection so an exit press does not immediately trigger normal tare, timer, shutdown, or BLE-connected behavior.

The grinder number editor is an interaction reference for Back, decrement, increment, Save, and hold acceleration. Extract a generic helper before using it for unrelated settings rather than coupling new display UI to grinder-named functions.

## Persistent Display Options

For a setting that survives reboot:

1. add runtime state in `include/parameter.h`
2. add a typed key in `include/storage.h`
3. add its default and migration behavior
4. load it in `setup()`
5. save it from the menu action
6. update `tools/test_storage_migration_contract.py`
7. read `docs/AI_STORAGE_NOTES.md` before changing schema behavior

Use existing `storageGetBool()`, `storagePutBool()`, and other typed helpers. Do not add runtime EEPROM access.

A new independent key with a default does not automatically require a schema bump. Rename, type changes, or reinterpretation require an explicit migration plan.

Use `volatile` only when the state is genuinely shared with another task or interrupt context, and follow the owning subsystem's synchronization pattern.

## Change Routing

| Request | Primary edit points |
| --- | --- |
| Rename or reorder menu item | `include/menu.h` labels and pointer arrays |
| Add direct menu action | `include/menu.h` prototype, item, action, reachable array |
| Add submenu | `include/menu.h` declarations, array, `linkSubmenus()`, `selectMenu()` |
| Add persistent display option | `include/parameter.h`, `include/storage.h`, `src/hds.ino`, `include/menu.h`, storage contract test |
| Change controller, bus, or pins | `include/config.h`, `include/display.h` |
| Change menu font or rows | `include/display.h`, `showMenu()`, `linesPerPage` |
| Change weight font, format, unit, position | `include/display.h`, `drawWeight()` |
| Change timer position | `drawWeight()`, `drawTime()`, existing `b_timeOnTop` |
| Add icon | XBM in `include/display.h`, helper and layer call in `src/hds.ino` |
| Change normal tare/timer button behavior | `src/hds.ino`, `include/finger_detection.h` |
| Change measurement stability | `pureScale()` and firmware notes, not only display code |

## Checks

Run:

```sh
pio run -e esp32s3
```

When persistent settings change:

```sh
python tools/test_storage_migration_contract.py
```

When this note or `docs/AI_REPO_MAP.md` changes:

```sh
python tools/test_ai_docs_contract.py
```

Hardware checks for layout work:

- setup-menu first and last pages
- Circle and Square input and release behavior
- menu exit protection
- timer stopped and running
- both timer positions
- positive, negative, large, and overweight values
- BLE, WiFi, battery, charging, grinder, tare, ADC recovery, shutdown, debug, and about states
- both rotations
- display sleep and soft-sleep wake
- clipping, controller offset, flicker, and partial-page artifacts

A successful build does not prove the correct OLED controller, column offset, or physical readability. State explicitly when hardware validation was not performed.
