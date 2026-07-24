# Half Decent Scale — Extensible Firmware & Extension Marketplace

**Engineering specification · v1.0 · 2026-07-24**

| | |
|---|---|
| **Device** | ESP32-S3-WROOM-1 N16 |
| **Base firmware** | 3.1.x · PlatformIO |
| **Source** | github.com/decentespresso/openscale |
| **License** | GPL-3.0 |

A specification for a firmware architecture that lets scale owners compose custom firmware from open-source extensions, build it in CI, validate it against real hardware, and install it over the air — with an on-device recovery path that cannot be locked out.

> **Status of this document.** The SDK surface in [§5](#5-extension-sdk-proposed) is a proposal for review and ratification; names and grouping are illustrative, the structure is the proposal. Open decisions are listed in [§14](#14-open-decisions).

## Key parameters

- **Extension model:** native C, compiled in — no interpreter, no sandbox.
- **Memory envelope:** 512 KB SRAM, no PSRAM; ~70 KB free heap with WiFi up.
- **Delivery:** seal-bound OTA from decentespresso.com, plus USB flasher.
- **Safety net:** CI + hardware pre-flight validation; three-failed-boot auto-revert.

## Contents

1. [Goals & scope](#1-goals--scope)
2. [Platform constraints](#2-platform-constraints)
3. [Architecture overview](#3-architecture-overview)
4. [Extension model & categories](#4-extension-model--categories)
5. [Extension SDK (proposed)](#5-extension-sdk-proposed)
6. [Packaging & discovery](#6-packaging--discovery)
7. [Build & delivery pipeline](#7-build--delivery-pipeline)
8. [Pre-flight safety gate](#8-pre-flight-safety-gate)
9. [Delivery, ownership & phone-home](#9-delivery-ownership--phone-home)
10. [On-scale UX & recovery](#10-on-scale-ux--recovery)
11. [Flash partitioning](#11-flash-partitioning)
12. [Marketplace & moderation](#12-marketplace--moderation)
13. [Work breakdown](#13-work-breakdown)
14. [Open decisions](#14-open-decisions)

---

## 1. Goals & scope

Let owners of the Half Decent Scale extend and re-purpose the device with community-written code, without Decent gatekeeping the software — while keeping every scale recoverable to a known-good state.

The system spans three surfaces:

- **Firmware** — a native extension framework so third-party C code (drivers, UI, standalone apps) can be compiled into a custom image, plus an on-scale settings/app manager and recovery path.
- **Build service** — GitHub Actions, run in a Decent-controlled context, that assembles a base firmware version plus a chosen set of extensions into one image, validates it, and publishes it.
- **Website** — a marketplace at `decentespresso.com/support/decentscale` where an owner discovers extensions, composes a build, and installs it onto a scale bound to their account.

**Representative use cases that must be supported:**

- A driver for a Bluetooth pressure sensor (Pressensor, SEP) that adds an on-scale UI replacing the vendor's phone app.
- Drivers for BLE/MQTT/WiFi smart power plugs.
- A **stop-at-weight** application: the user sets a target weight; a tap of the square button switches on a smart plug (through a plug driver), and the plug switches off when the scale reaches the target — implementing **grind-by-weight** for a coffee grinder. This exercises the driver↔app composition directly and is worked through in [§5](#worked-example--grind-by-weight).
- Self-contained applications with no espresso relationship at all (e.g. a hair-dye mixing guide that walks a user through component quantities).

**Explicitly out of scope:** an on-device scripting runtime or sandbox. Extensions are native code with full hardware access and no isolation. This is a deliberate trade (see [§2](#2-platform-constraints), [§4](#4-extension-model--categories)): the memory envelope does not justify an interpreter, and the accepted risk of a bad extension is contained by the safety net rather than by isolation.

---

## 2. Platform constraints

All figures below are measured on current hardware and firmware, not estimated. Every design choice in this document derives from them.

| Constraint | Value | Design implication |
|---|---|---|
| **RAM** | 512 KB SRAM, **no PSRAM**. Free heap ≈ 70 KB with WiFi up, ≈ 157 KB BLE-only (WiFi off). Firmware reboots below 15 KB free. | No headroom for a VM/interpreter. Extensions must be heap-frugal; the recipe composer must budget heap across the selected set ([§8](#8-pre-flight-safety-gate)). |
| **Threading** | Display, I²C, SPI, the scale front-end and GPIO are only safe on the main `loop()`. AsyncTCP and the ADC run on core 1. | Extension hooks run on the main loop, must be **run-to-completion and non-blocking**. Blocking trips the task watchdog and misses the BLE heartbeat. |
| **Flash** | 16 MB (N16). Current build targets an 8 MB partition table (`partitions/default_8MB.csv`). | Move to a 16 MB layout ([§11](#11-flash-partitioning)) to fit dual OTA slots plus a golden recovery image. |
| **Existing OTA** | Staged OTA with dual app slots and LittleFS rollback; today it verifies a signed manifest (`keys/ota`). No ESP32 Secure Boot or flash encryption is enabled. | The OTA transport is reused. Signature verification is removed ([§9](#9-delivery-ownership--phone-home)); ownership, not signing, gates delivery. USB flashing was never gated and remains the universal recovery path. |
| **Peripherals** | Load cell via ADS1232; SH1106/SH1116 128×64 mono OLED; ADS1115 battery monitor; BMA400 motion; two physical buttons — round on the left, square on the right; BLE + WiFi. | Defines the SDK's hardware surface ([§5](#5-extension-sdk-proposed)). Two buttons constrain on-scale UX to list-and-select, never text entry. |
| **Host I/O bus** | USB-C carries power *and* a bidirectional data/command stream (Decent binary + ASCII). WiFi (WebSocket) carries the identical protocol. Both stream telemetry out and accept commands in. | The SDK exposes this as a unified command/telemetry bus with USB and WiFi as equivalent channels; extensions can observe, extend, take over, or handle commands on either ([§5](#host-facing-command--telemetry-bus--usb-and-wifi)). |

---

## 3. Architecture overview

An **extension** is a GitHub repository of C/C++ source that registers itself against a stable firmware API. A **recipe** names a base firmware version and a pinned set of extensions. The build service compiles a recipe into one firmware image, runs it through a pre-flight gate (CI checks plus a real scale on a test rig), and publishes it. An owner installs a published image onto a scale their account owns, over the air or by USB. On the scale, a fixed Settings surface lists the compiled-in apps, selects a boot default, fetches firmware, and provides recovery.

1. **Author** — writes an extension (optionally AI-assisted from the SDK spec), tags the repo, publishes to GitHub.
2. **Discover & compose** — owner browses tagged extensions on the website and selects a set → a recipe.
3. **Build & validate** — Decent CI assembles the recipe, runs the pre-flight gate including a hardware-in-the-loop test on a real scale.
4. **Publish & assign** — the image is published and assigned to the owner's scale(s) by seal number.
5. **Install** — the scale fetches its assigned build over WiFi (or via the USB flasher) and boots it. Recovery is always available.

---

## 4. Extension model & categories

All extensions are native code compiled into the image and run without isolation. They differ by role, declared in the manifest ([§6](#6-packaging--discovery)); the category drives marketplace filtering and conflict rules.

| Category | Role | Example |
|---|---|---|
| `driver` | Adds support for an external device (BLE/WiFi/MQTT). Registers a data source or capability other extensions can consume. | Pressensor pressure sensor; Tasmota/BLE smart plug. |
| `ui` | Adds or modifies on-scale screens/readouts within the standard app. | Alternate shot screen; pressure overlay. |
| `app` | A self-contained application selectable as the boot default. Owns the screen while active. | Hair-dye mixing guide; grind-by-weight; timer. |
| `integration` | Connects the scale to a network service; no direct UI. | Publish weight over MQTT; webhook on shot end. |

Apps consume drivers through **typed capabilities** rather than by depending on a specific extension ([§5](#driver-capabilities--inter-extension-services)). An app such as grind-by-weight consumes a generic *switch* capability, so it works with any smart-plug driver — BLE, WiFi, or MQTT — that provides one.

### Failure modes the framework must defend against

Without a sandbox, an extension can crash or brick the device. Three failure modes account for nearly all risk, and every safety mechanism ([§8](#8-pre-flight-safety-gate), [§10](#10-on-scale-ux--recovery)) targets them specifically:

- **Stack overflow** — a large local array or deep recursion corrupts memory and causes a boot loop. Detected deterministically at build time.
- **Blocking the main loop** — a blocking call in a hook trips the watchdog and misses the BLE heartbeat. Prevented by the non-blocking hook contract and a blocking-call gate.
- **Heap exhaustion** — excess allocation (especially a driver's BLE connection) pushes free heap toward the reboot floor. Measured on real hardware and budgeted before build.

---

## 5. Extension SDK (proposed)

> **For review.** The surface below is a first cut intended to be trimmed and ratified. Names and grouping are illustrative; the structure is the proposal.

The SDK is the contract authors write against and the document their AI tools read. It has two parts: a **registration + lifecycle model** that keeps extensions decoupled from firmware internals, and a **host API** exposing the hardware.

### Registration

Each extension defines one descriptor and registers it into a dedicated linker section. The base firmware enumerates that section at boot — it never hard-codes a list, so adding an extension is purely additive.

```c
// openscale_sdk.h — descriptor + registration
typedef enum { OS_CAT_DRIVER, OS_CAT_UI, OS_CAT_APP, OS_CAT_INTEGRATION } os_category_t;

typedef struct {
    const char   *id;           // reverse-DNS, e.g. "com.pressensor.pressure"
    const char   *name;         // shown in the on-scale app list
    uint16_t      api_version;  // OS_API_VERSION this was built against
    os_category_t category;
    const os_hooks_t *hooks;    // lifecycle callbacks (all optional)
} os_extension_t;

// Registers the descriptor into the "os_ext" section for boot-time enumeration.
#define OS_REGISTER_EXTENSION(desc) \
    __attribute__((used, section("os_ext"))) \
    static const os_extension_t *const _os_reg_##desc = &desc;
```

### Lifecycle hooks

All hooks run on the main loop, must return promptly (target < a few ms), and must not block. `on_draw` renders into a system-owned framebuffer that the firmware flushes to the OLED after the call.

```c
typedef struct {
    void (*on_register)(void);                 // once at boot, before any app starts
    void (*on_start)(void);                     // this app became active
    void (*on_stop)(void);                      // this app was deactivated
    void (*on_tick)(uint32_t dt_ms);            // every loop iteration while active
    void (*on_weight)(float grams, uint32_t t_ms);   // new stable/raw weight sample
    void (*on_button)(const os_button_evt_t *e); // short / long / hold, per button
    void (*on_draw)(os_gfx_t *g);               // render one frame (system flushes)
    void (*on_ble_notify)(const os_ble_evt_t *e); // a subscribed characteristic fired
} os_hooks_t;
```

### Host API — full hardware access

Grouped by subsystem. Radio and network calls are asynchronous: they return immediately and deliver results via hooks or callbacks, never by blocking.

```c
// Scale & sensors
float os_scale_weight(void);            bool  os_scale_is_stable(void);
void  os_scale_tare(void);              void  os_scale_zero(void);
int   os_battery_percent(void);         float os_battery_volts(void);
bool  os_charger_present(void);         void  os_motion_read(os_vec3_t *accel); // BMA400

// Display (128x64 mono) — draw inside on_draw()
void  os_gfx_clear(os_gfx_t*);          void os_gfx_text(os_gfx_t*,int x,int y,const char*,os_font_t);
void  os_gfx_number(os_gfx_t*,int x,int y,float v,int decimals);
void  os_gfx_rect(os_gfx_t*,int x,int y,int w,int h,bool fill);   void os_gfx_line(/*...*/);

// Persistence — per-extension namespace in NVS/LittleFS
bool  os_store_put(const char *key,const void *buf,size_t len);
bool  os_store_get(const char *key,void *buf,size_t *len);

// Non-blocking timers
os_timer_t os_every(uint32_t ms,void(*cb)(void*),void *arg);
os_timer_t os_after(uint32_t ms,void(*cb)(void*),void *arg);   void os_timer_cancel(os_timer_t);

// BLE client (drivers) — async; data arrives via on_ble_notify
os_ble_h os_ble_connect(const os_ble_filter_t *f);   // by name / service UUID
bool os_ble_subscribe(os_ble_h,os_uuid_t svc,os_uuid_t chr);
bool os_ble_write(os_ble_h,os_uuid_t svc,os_uuid_t chr,const uint8_t*,size_t);

// Network / integration — async callbacks
bool os_net_online(void);
void os_http_get(const char *url,os_http_cb cb,void *arg);
bool os_mqtt_publish(const char *topic,const uint8_t *payload,size_t n);

// System
void os_app_switch(const char *ext_id);   uint32_t os_free_heap(void);
void os_beep(uint16_t hz,uint16_t ms);      void os_log(os_level_t,const char *fmt,...);
```

### Buttons

Two physical buttons — round on the left, square on the right — each producing short press, long press, and hold events. Events are delivered to `on_button`; instantaneous state is also pollable. Two gestures are reserved by the system and never delivered to extensions: both buttons held at power-on (enters Settings/recovery, [§10](#10-on-scale-ux--recovery)) and the reserved return-to-Settings gesture.

```c
typedef enum { OS_BTN_LEFT_ROUND   = 0,   // round button, left
               OS_BTN_RIGHT_SQUARE = 1 } os_button_t;  // square button, right

typedef enum {
    OS_ACT_SHORT,        // short press and release
    OS_ACT_LONG,         // held past the long-press threshold
    OS_ACT_HOLD_BEGIN,   // press-and-hold started
    OS_ACT_HOLD_REPEAT,  // fires repeatedly while held
    OS_ACT_HOLD_END      // released after a hold
} os_button_action_t;

typedef struct {
    os_button_t        button;
    os_button_action_t action;
    uint32_t           held_ms;   // duration held (LONG / HOLD_*)
} os_button_evt_t;

bool os_button_down(os_button_t b);       // instantaneous poll
```

### Host-facing command & telemetry bus — USB and WiFi

USB-C carries power and a bidirectional data/command stream; WiFi carries the identical protocol over WebSocket. The SDK models them as equivalent *channels* on one bus. An extension can register handlers for inbound commands, add/remove elements of the outbound telemetry stream via filters, take over a channel to become its sole emitter, or drop to raw bytes. Passing `OS_CH_ALL` applies to every channel at once, so USB and WiFi are handled uniformly. (The scale's BLE peripheral protocol is a candidate third channel on the same bus.)

```c
typedef enum { OS_CH_USB = 1<<0, OS_CH_WIFI = 1<<1, OS_CH_ALL = 0xFF } os_channel_t;
typedef struct { uint16_t opcode; const uint8_t *data; size_t len; } os_msg_t;

// Inbound — handle commands from a host. Return CONSUMED to stop, PASS to fall through.
typedef enum { OS_CMD_CONSUMED, OS_CMD_PASS } os_cmd_result_t;
typedef os_cmd_result_t (*os_cmd_handler_t)(os_channel_t src,
                                            const os_msg_t *in, os_msg_t *reply);
void os_cmd_register(uint16_t opcode, os_channel_t ch, os_cmd_handler_t h);
void os_cmd_unregister(uint16_t opcode, os_channel_t ch);

// Outbound — extend or reshape the telemetry stream.
void os_stream_emit(os_channel_t ch, const os_msg_t *rec);   // add a record
typedef enum { OS_STREAM_KEEP, OS_STREAM_DROP } os_stream_action_t;
typedef os_stream_action_t (*os_stream_filter_t)(os_channel_t dst, os_msg_t *rec);
os_filter_h os_stream_add_filter(os_channel_t ch, os_stream_filter_t f);  // add/remove/modify
void        os_stream_remove_filter(os_filter_h);

// Take over — become the sole emitter on a channel (suppresses default telemetry).
void os_stream_take_over(os_channel_t ch);    void os_stream_release(os_channel_t ch);

// Raw bytes — for protocols the message model does not cover.
typedef void (*os_raw_rx_t)(os_channel_t src, const uint8_t *data, size_t len);
void os_raw_on_receive(os_channel_t ch, os_raw_rx_t cb);
void os_raw_send(os_channel_t ch, const uint8_t *data, size_t len);
```

### Driver capabilities — inter-extension services

An app must use a driver without depending on which one. A driver publishes a *typed capability*; an app looks the capability up and calls it through a small vtable. This is what lets grind-by-weight drive any smart plug regardless of whether its driver speaks BLE, WiFi, or MQTT.

```c
typedef enum { OS_CAP_SWITCH, OS_CAP_PRESSURE, OS_CAP_TEMP /* ... */ } os_cap_type_t;

// A switchable relay / smart plug.
typedef struct os_switch {
    void (*on)(struct os_switch*);
    void (*off)(struct os_switch*);
    bool (*is_on)(struct os_switch*);
    const char *label;
} os_switch_t;

// Driver side — publish an instance (typically in on_register).
void  os_cap_provide(os_cap_type_t type, void *instance);
// App side — find providers.
void *os_cap_find(os_cap_type_t type);                 // first match, or NULL
int   os_cap_list(os_cap_type_t type, void **out, int max);
```

### Versioning & the authoring contract

- `OS_API_VERSION` is a monotonically increasing integer. An extension records the version it was built against; the build refuses a recipe whose extensions require a version the base firmware does not provide.
- **Documented obligations** for authors: hooks return quickly and never call blocking primitives; no dynamic allocation in hot paths; local buffers kept small (a stack budget is published); all persistence through `os_store_*`. These are enforced where possible by the pre-flight gate ([§8](#8-pre-flight-safety-gate)).
- **Deliverables:** `docs/extension-sdk.md` (the normative spec), the SDK headers, and an `openscale-extension-template` repository (a buildable skeleton with a passing example extension).

### Worked example — grind-by-weight

A required use case and a compact exercise of the whole SDK. An `app` that, on a tap of the square (right) button, switches on a smart plug via any installed switch driver and switches it off when the scale reaches a target weight. Its manifest declares `"requires": ["cap:switch"]`, so the composer guarantees a compatible plug driver is present in the build.

```c
static os_switch_t *g_plug;
static float g_target = 30.0f;      // grams; user-set, persisted
static bool  g_running;

static void on_start(void) {
    size_t n = sizeof g_target;
    os_store_get("target", &g_target, &n);   // restore last target
    g_plug = os_cap_find(OS_CAP_SWITCH);      // any plug driver in the build
}
static void on_button(const os_button_evt_t *e) {
    if (e->button == OS_BTN_RIGHT_SQUARE && e->action == OS_ACT_SHORT && g_plug) {
        g_running = true;
        g_plug->on(g_plug);                   // start the grinder
    }
}
static void on_weight(float grams, uint32_t t_ms) {
    if (g_running && grams >= g_target) {
        g_plug->off(g_plug);                  // stop at target
        g_running = false;
        os_beep(2000, 120);                   // signal done
    }
}
static void on_draw(os_gfx_t *g) {
    os_gfx_clear(g);
    os_gfx_number(g, 0,  0, os_scale_weight(), 1);   // live weight
    os_gfx_number(g, 0, 32, g_target,          1);   // target
}
static const os_hooks_t hooks = {
    .on_start = on_start,   .on_button = on_button,
    .on_weight = on_weight, .on_draw = on_draw,
};
static const os_extension_t ext = {
    .id = "com.decent.grind-by-weight", .name = "Grind by Weight",
    .api_version = OS_API_VERSION, .category = OS_CAT_APP, .hooks = &hooks,
};
OS_REGISTER_EXTENSION(ext);
```

The app never names a specific plug driver — it consumes `OS_CAP_SWITCH`, so it works with any BLE/WiFi/MQTT plug whose driver provides that capability. Left-button handling to adjust the target and persistence writes are omitted for brevity.

---

## 6. Packaging & discovery

An extension is discoverable through two mechanisms working together:

- **GitHub repository topic** `openscale-extension`. The website enumerates candidates via the GitHub Search API — no central registry to maintain.
- **Root manifest** `openscale-extension.json`, the authoritative metadata the topic points at.

```json
{
  "id": "com.pressensor.pressure",
  "name": "Pressensor Pressure",
  "description": "BLE pressure sensor driver + on-scale readout",
  "author": "pressensor",
  "category": "driver",
  "entry": "src/pressure.cpp",
  "api_version": 3,
  "base_versions": ["3.1.x"],
  "visibility": "public",
  "requires": [],
  "conflicts": []
}
```

Visibility is `public` or `private`; a private extension is simply a private GitHub repository, surfaced to its owner after a GitHub App OAuth login that records the owner's GitHub username against their Decent account. `requires`/`conflicts` express dependencies and mutual exclusions (only one boot `app` at a time). A `requires` entry may name a specific extension id or a capability (e.g. `cap:switch`); the composer ensures at least one provider of each required capability is included in the recipe.

---

## 7. Build & delivery pipeline

The buildable unit is a **recipe**: a base firmware version, a pinned list of extension repositories at specific commits, and per-extension settings. Each unique recipe produces one on-demand CI build, cached by recipe hash; there is no pre-built combinatorial matrix.

1. **Compose** — the website emits `recipe.json` from the owner's selection.
2. **Assemble & validate** — Decent CI checks out the base plus each extension at its pinned commit; validates every manifest's `api_version` against the base.
3. **Pre-flight gate ([§8](#8-pre-flight-safety-gate))** — deterministic checks, simulator pre-screen, then a real scale on a test rig. A failure here never produces a deliverable image.
4. **Publish** — the image plus its `manifest.json` (reusing the current OTA format) is published; heap/size metrics from the gate are attached to the build record.
5. **Assign & notify** — a webhook updates website build status. The build is assignable to scales the owner's account owns ([§9](#9-delivery-ownership--phone-home)).

> **Build service placement.** The build and publish steps run in a **Decent-controlled GitHub Actions context**, not in the author's repository. This is required so that the pre-flight gate and the published metrics are trustworthy and cannot be bypassed by an author's own workflow.

---

## 8. Pre-flight safety gate

Because there is no runtime sandbox, this gate is the primary defense. It predicts whether an image will brick a scale before that image is offered for install. Deterministic tooling forms the hard gates; an LLM pass is advisory.

| Stage | Type | What it does |
|---|---|---|
| **Worst-case stack analysis** | hard | `-fstack-usage` plus a call-graph analyzer (`puncover`) computes worst-case depth and fails it against the task stack budget. This is what clears stack-overflow risk. |
| **Blocking-call blocklist** | hard | Call-graph scan rejects known-blocking calls (`delay`, busy-waits, synchronous I/O) reachable from any hook. Enforces the non-blocking contract. |
| **Compiler & static analysis** | hard | `-Wall -Wextra -Werror`, `cppcheck`/`clang-tidy` for undefined-behavior patterns. |
| **Simulation pre-screen** | hard | Wokwi/QEMU: does the image boot without an obvious panic? Cheap and parallel — filters broken builds before they occupy hardware. |
| **Hardware-in-the-loop** | hard (authoritative) | A real scale on the build server's USB port. Source of the published heap metrics. See below. |
| **LLM review (DeepSeek)** | advisory | Explains likely defects in plain language and recommends abort; explicit override. Reliable for blocking patterns; not relied on for stack analysis. |

### Hardware-in-the-loop test rig

A Half Decent Scale is connected to a build-server USB port. The rig flashes the candidate image and exercises it over pure USB serial: confirm boot banner and absence of panic, read heap and minimum-heap, then command a weight stream and verify that samples arrive at the expected rate *and that their values change*. The rig's scale is placed on a deliberately vibrating surface so the load cell produces continuously varying (noisy) readings; a stream of changing values confirms both that the main loop is actively sampling and that the load-cell signal path is live, whereas a frozen value would indicate a stalled loop or a broken sensor path — a distinction a static reading could not make.

Beyond USB, the rig independently verifies the scale's other host transports: the server opens a BLE connection to the freshly flashed scale and reads a weight notification, and joins it over WiFi to read weight from the WebSocket. Each confirms that transport's stack initializes on the candidate image and that an extension has not broken BLE or WiFi reachability — reusing the existing bench clients (a BLE central and the WebSocket test).

The rig recovers itself by reflashing stock via the ROM bootloader (USB auto-reset works on this board), so a hung or boot-looping candidate cannot permanently brick it; this makes it safe to run untrusted community firmware. The simulator cannot model the BLE/WiFi radio or the load cell, so the rig is the only source of true driver heap cost and sensor-path validation. A reference BLE peripheral attached to the rig allows sensor drivers to be exercised end-to-end.

> **Firmware support needed for the rig.** Heap telemetry currently prints only when WiFi is up. Add a plain USB-serial "report heap" command to the test-harness firmware so the entire rig sequence — boot, heap, loop-alive — runs with no networking. The weight stream already works over USB serial.

### Per-extension memory budget (marketplace data)

The gate produces the numbers; they are published per extension so owners can budget before building.

| Figure | Source |
|---|---|
| Static RAM + flash delta | Linker map at build time (deterministic) |
| Idle heap delta | HIL rig — real scale, idle |
| Worst-case heap-under-load delta | HIL rig — real scale under load |

The recipe composer sums these across a selection and warns or blocks before build if projected free heap approaches the reboot floor — catching combinations that will not fit.

---

## 9. Delivery, ownership & phone-home

Firmware signing is removed. Delivery is gated by **ownership**, anchored in Decent's sales database, which is authoritative. Two install paths:

| Path | Gate |
|---|---|
| **WiFi** — from the owner's account | The scale receives only builds assigned to its seal number's owner. |
| **USB** — [hds-updater](https://decentespresso.github.io/hds-updater/) | Physical access. Stock is public, so any scale is recoverable to known-good. |

### The seal-number ownership chain

The website is password-protected and lists only the seal numbers the signed-in account owns per the sales record. A build can only be assigned to a scale whose seal the account owns. A scale reports its seal on WiFi and receives only its assigned builds — Decent stock plus the owner's named builds. Because ownership is enforced by the sales database at the point of *build and assignment*, a scale reporting a seal it does not own gains nothing: it cannot cause firmware to be built or assigned for that seal. Firmware images are not treated as secrets, so no device-side authentication token is required.

### Phone-home

The firmware POSTs to a new endpoint, `https://decentespresso.com/support/api/`, on WiFi connection, reporting its seal number, model, PCB revision, firmware version, and current build id. When a scale first appears, the owner must **explicitly accept it into their account** before it is associated and manageable. This acceptance step is the association handshake and the point at which the owner confirms the physical device is theirs.

---

## 10. On-scale UX & recovery

A single, non-removable Settings surface is the control and recovery point. It is always compiled in, cannot be replaced or hidden by any extension, and owns a reserved return gesture so a misbehaving app cannot trap the user.

| Function | Behavior |
|---|---|
| **Two-button boot** | Powering on with **both buttons held** always enters Settings, before any app runs. This is the guaranteed recovery entry. |
| **Apps** | Lists every registered app; each launchable; one radio-selected boot default. The `conflicts` rule keeps a single boot app. |
| **Get firmware** | On WiFi, lists the builds assigned to this scale (matched by seal) — stock plus the owner's named builds — selected and installed with two buttons. No URL entry. |
| **WiFi** | Network join — prerequisite for firmware fetch and phone-home. |
| **Recovery** | Revert to the golden stock image ([§11](#11-flash-partitioning)), available offline. |

> **Automatic boot-loop recovery.** The bootloader counts consecutive failed boots. **After three failed boots, the firmware automatically reverts to the last known-good image** and records the event as a health signal for the offending build. Combined with the two-button Settings entry and the USB flasher, a scale cannot be left unrecoverable.

---

## 11. Flash partitioning

Migrate from the 8 MB table to a 16 MB layout matching the N16 board, providing dual OTA slots and a golden recovery image. Illustrative:

| Partition | ~Size | Purpose |
|---|---|---|
| `nvs`, `otadata`, `phy` | ~40 KB | Config, OTA state, RF calibration |
| `ota_0` / `ota_1` | ~4 MB each | Active + staged app slots |
| `recovery` | ~2.5 MB | Golden stock image for offline revert and boot-loop recovery |
| `littlefs` | remainder | web_apps assets, per-extension storage |

---

## 12. Marketplace & moderation

The marketplace lists extensions by category with compatibility flags against a scale's firmware, and drives recipe composition. Publication rules differ by visibility:

- **Private extensions** — visible only to their owner, available immediately, no review.
- **Public extensions** — must pass a **moderation approval process** before they are discoverable by other users.

> **Moderation process — to be defined.** The approval process is not yet specified. Proposed first cut for discussion: (1) the pre-flight gate ([§8](#8-pre-flight-safety-gate)) must pass — compiles, boots on the rig, within budgets; (2) a manifest/metadata check — valid schema, correct category, coherent name/description; (3) a content/safety review (human or AI-assisted) for malicious intent or policy violations; (4) approval flips the listing to publicly discoverable. Re-review triggers on new releases. Requires defining reviewer roles, SLA, an appeals/takedown path, and whether step 3 is automated, manual, or staged by author reputation.

---

## 13. Work breakdown

Sequenced so each phase is independently useful and testable. Firmware and web tracks can proceed in parallel once the SDK (P0) is fixed.

### P0 — SDK & foundations
- Ratify the SDK surface ([§5](#5-extension-sdk-proposed)); publish `docs/extension-sdk.md`, headers, and the template repo.
- Define and migrate to the 16 MB partition layout ([§11](#11-flash-partitioning)), including the recovery partition.
- Specify the `openscale-extension.json` schema and the `openscale-extension` topic convention.

### P1 — On-scale framework
- Extension registration (linker-section enumeration) and the hook dispatch loop.
- Non-removable Settings app: app list, boot-default selection, WiFi, firmware fetch, recovery.
- Two-button-boot recovery entry; three-failed-boot auto-revert; golden-image restore.

### P2 — Build service & pre-flight gate
- Decent-controlled Actions: recipe assembly, `api_version` validation, image + manifest publish.
- Deterministic gates (stack, blocking, static analysis, size); DeepSeek advisory pass.
- Hardware-in-the-loop rig (productionize the bench harness); serial heap-report command; publish per-extension metrics.

### P3 — Website & delivery
- Phone-home endpoint `/support/api/`; per-scale acceptance; seal→account association.
- Marketplace: discovery via GitHub topic, recipe composer with heap budgeting, build status.
- Seal-bound OTA fetch on the scale; assignment of builds to owned seals.

### P4 — Private extensions & moderation
- GitHub App OAuth for private-repo discovery.
- Public-extension moderation pipeline ([§12](#12-marketplace--moderation)) and health/quarantine signals.

---

## 14. Open decisions

1. **SDK surface ratification** — review [§5](#5-extension-sdk-proposed) and trim/extend the hook set and host API before headers are frozen.
2. **Capability taxonomy** — pin the initial set of `os_cap_type_t` capabilities (switch, pressure, temperature, flow, generic sensor, …); these are the contract between driver authors and app authors.
3. **Moderation process** — define the public-extension approval pipeline outlined in [§12](#12-marketplace--moderation) (reviewer roles, automation level, appeals/takedown).
4. **Stack & heap budgets** — set the concrete numbers the gate enforces (task stack ceiling, max static-RAM growth, minimum projected free-heap floor).
5. **Boot-loop health reporting** — confirm whether auto-revert events are reported to the marketplace and how they affect a build's or extension's standing.

---

*Figures measured on ESP32-S3-WROOM-1 N16 hardware and the current openscale firmware. The SDK surface in §5 is proposed and subject to ratification.*
