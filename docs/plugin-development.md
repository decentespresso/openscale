# Developing an OpenScale Plugin

OpenScale plugins let contributors add optional features, hardware integrations, web applications, or other functionality without permanently adding every implementation to the normal firmware build.

This guide is written for contributors with different levels of experience. You do **not** need to be an experienced embedded developer to start. You may also use an AI coding assistant to help create a plugin, provided the resulting plugin follows the same repository, build, testing, security, licensing, and hardware-validation rules as any other contribution.

The important rule is:

> **AI may help produce the work, but it does not change the acceptance requirements.**

A plugin must still build correctly, declare its real dependencies, stay within its defined boundaries, pass the required tests, and make only hardware claims that have actually been verified.


## Choose Your Path

There are several kinds of OpenScale plugin contributions.

### 1. Webapp or asset plugin

Choose this path if your plugin mainly adds:

* an on-device web interface;
* HTML, CSS, or JavaScript;
* documentation or a handbook;
* a configurator preview image;
* other runtime files.

This is usually the easiest type of plugin and may require little or no firmware C++ work.

Start with:

* **Package Layout**
* **Understanding `plugin.json`**
* **Webapp Files**
* **Configurator Presentation**
* **Validate Before Review**

You can skip most firmware-patching sections unless your plugin also changes firmware behavior.

### 2. Existing built-in feature

Some functionality already exists in the firmware but appears in the plugin catalog as an optional feature.

`plugins/grind-by-weight/` is the main reference for this type of integration.

Use it to understand:

* compile-time feature gates;
* dependency checks;
* menu integration;
* setup and loop hooks;
* persistent settings;
* power behavior;
* contract tests.

Do not copy dependencies such as networking unless your own feature actually needs them.

### 3. Firmware-changing plugin

Choose this path when your plugin changes the OpenScale firmware itself.

This requires more care because the plugin is stored as a version-specific patch that is temporarily applied during a custom build.

The normal firmware source tree remains unchanged.

The general workflow is:

```text
Develop feature
      |
      v
Test implementation
      |
      v
Generate patch
      |
      v
Create plugin package
      |
      v
Declare dependencies
      |
      v
Run validation
      |
      v
Submit for review
```

Firmware-changing plugins should follow the complete guide.

## Using AI to Develop a Plugin

AI-assisted contributions are allowed.

An AI coding assistant can help with tasks such as:

* exploring the repository;
* identifying existing implementation patterns;
* creating or modifying C++ code;
* creating a webapp;
* preparing `plugin.json`;
* generating tests;
* generating a patch;
* running repository validation tools;
* checking compile errors;
* explaining failures;
* preparing documentation.

However, an AI-generated plugin is subject to exactly the same requirements as a manually written plugin.

In particular, AI must not be used as a substitute for evidence.

For example:

* a successful compile does not prove hardware communication works;
* a menu appearing does not prove a sensor works;
* AI-generated code does not establish license compatibility;
* guessed resource usage is not a measured resource budget;
* simulated behavior does not count as physical hardware testing;
* passing a test does not justify claims outside what that test actually covers.

When hardware is unavailable, say so explicitly.

For example:

```text
Validated:
- normal firmware build
- plugin build
- custom firmware build
- menu appears on device

Not validated:
- physical sensor communication
- calibration accuracy
- electrical compatibility
- radio coexistence
```

This is acceptable. Claiming unperformed testing is not.

# AI Contributor Prompt

AI-assisted contributors follow the same requirements above.

Provide the AI with:

* implementation source or feature description;
* plugin ID;
* firmware ref;
* available hardware;
* relevant provenance or licensing information.

Then use:

```text
Work in decentespresso/openscale.

Read docs/AI_REPO_MAP.md, docs/AI_PLUGIN_NOTES.md,
docs/AI_BUILD_NOTES.md, and docs/plugin-development.md.

Inspect plugins/grind-by-weight only as the built-in compile-gating
reference and plugins/default-web-apps as the asset/webapp reference.
Reuse existing build and catalog tools.

Turn <implementation PR, branch, or description> into the approved
patch plugin <plugin-id> for firmware ref <firmware-ref>.

Keep the normal firmware source tree unchanged outside
plugins/<plugin-id> on the catalog branch.

Store implementation changes in a version-specific patch. Declare only
real feature dependencies, plugin dependencies, recommendations, and
conflicts. Do not add arbitrary download URLs or uploaded-code paths.

For a firmware-changing plugin, default HDS_ENABLE_<PLUGIN> to
HDS_CUSTOM_BUILD, keep it disabled in an applied normal esp32s3 build,
and define esp32s3-<plugin-id> for direct validation.

Guard every plugin-specific include, global, menu, display, button,
setup, loop, storage, network, hardware, and power integration.

Keep WiFi, WebServer, WebSocket, and runtime LittleFS optional unless
the implementation actually uses them.

Regenerate both catalogs.

Run patch validation, catalog and custom-build tests, the normal
esp32s3 build, direct plugin-environment verification, and the
production custom build.

Report measured resource changes.

Distinguish source validation, local builds, device smoke testing, and
real peripheral validation. Never claim hardware testing that was not
performed.

Do not commit, push, merge, or close the source PR unless explicitly
asked.

Ask only when a dependency, hardware claim, license, provenance, or
security decision cannot be determined from the repository and supplied
implementation.
```

Replace every placeholder before use.

## What a Plugin Is

OpenScale plugins are approved inputs to the custom firmware build system.

Users select a plugin by its plugin ID in the configurator. The build service only accepts plugins that are already part of the trusted plugin catalog.

It does not accept arbitrary:

* source-code URLs;
* uploaded ZIP files;
* user-supplied patches.

A plugin package can contain:

* firmware patches;
* runtime files;
* dependency declarations;
* conflict declarations;
* resource budgets;
* presentation material;
* review metadata.

Firmware patches are applied only inside a temporary build checkout. They are not merged into the normal firmware source tree.

## Package Layout

The smallest plugin contains only `plugin.json`.

A firmware plugin might look like this:

```text
plugins/my-plugin/
|-- plugin.json
|-- patches/
|   |-- main.patch
|   `-- v3.2.0.patch
`-- assets/
    `-- page.html
```

A webapp plugin normally looks like this:

```text
plugins/example/
|-- plugin.json
`-- webapp/
    |-- index.html
    |-- app.css
    `-- app.js
```

Only `plugin.json` is mandatory.

Other directories are used only when the plugin needs them.

## Understanding `plugin.json`

A typical plugin manifest looks like this:

```json
{
  "schema": 2,
  "id": "my-plugin",
  "name": "My Plugin",
  "description": "Short user-facing description.",
  "tooltip": "Explain the hardware or service integration and important requirements.",
  "version": "1.0.0",
  "firmware_refs": ["main"],
  "requires": [],
  "depends_on": [],
  "conflicts": [],
  "conflicts_features": [],
  "recommends": {"features": [], "plugins": []},
  "patches": {
    "main": "patches/main.patch"
  },
  "assets": [],
  "budget": {
    "firmware_flash_bytes": 32768,
    "static_ram_bytes": 2048,
    "littlefs_bytes": 0
  }
}
```

The important fields are:

* `id` — permanent machine-readable plugin identifier;
* `name` — name shown to users;
* `description` — short user-facing explanation;
* `tooltip` — additional requirements or integration information;
* `version` — version of the plugin itself;
* `firmware_refs` — firmware versions or branches this plugin supports;
* `requires` — existing firmware features the plugin needs;
* `depends_on` — other plugins that must also be installed;
* `conflicts` — plugins that cannot be used together;
* `conflicts_features` — firmware features that cannot be used with the plugin;
* `recommends` — a combination of features or plugins known to work together;
* `patches` — firmware patches for supported firmware revisions;
* `assets` — files that need to be copied into the device filesystem;
* `budget` — expected flash, RAM, and filesystem usage.

Plugin IDs use lowercase letters, numbers, and hyphens.

For example:

```text
energy-monitor
smart-grinder
example-webapp
```

Keep the plugin version separate from the firmware version.

Resource budgets should be based on measured changes with reasonable headroom. They should not be arbitrary maximum values.

## Dependencies in Plain Language

Declare only dependencies that the plugin actually needs.

Use `requires` for firmware capabilities.

For example:

```json
"requires": ["wifi", "webserver"]
```

means the plugin requires those firmware features.

Typical examples are:

* `wifi` when networking is used;
* `webserver` when the plugin provides HTTP behavior or a web interface;
* `littlefs` when the plugin needs files available from the runtime filesystem;
* `websocket` when the webapp uses the WebSocket API such as `/snapshot`.

Do not add dependencies just because another example plugin uses them.

Use `depends_on` when your plugin needs another plugin.

For example:

```json
"depends_on": ["default-web-apps"]
```

means that plugin must also be present.

Use `conflicts` when two plugins cannot safely be used together.

Use `conflicts_features` when the plugin is incompatible with an existing firmware feature.

The build system resolves dependencies before checking the final combination.

More complicated dependency and conflict behavior is described later in this guide for contributors who need it.

## Firmware Plugin Development

The following sections apply mainly to plugins that change firmware code.

Develop the feature on a separate implementation branch created from the exact firmware revision the plugin will support.

The feature must remain disabled in the normal firmware build.

Guard the integration behind a single compile gate such as:

```c
HDS_ENABLE_MY_PLUGIN
```

The applied patch must support:

```c
#ifndef HDS_ENABLE_MY_PLUGIN
#if HDS_CUSTOM_BUILD
#define HDS_ENABLE_MY_PLUGIN 1
#else
#define HDS_ENABLE_MY_PLUGIN 0
#endif
#endif
```

This means:

* normal firmware build: plugin disabled;
* custom build after the patch is applied: plugin enabled;
* dedicated plugin test environment: plugin explicitly enabled.

From this point onward, the firmware-specific rules in this guide are normative. AI-assisted and manually written implementations follow the same rules.

## Before Submitting

A contributor should be able to answer these questions:

* What does the plugin do?
* Which firmware revision does it support?
* Does it modify firmware or only add files?
* Which features does it really require?
* Does it depend on another plugin?
* What hardware was actually tested?
* What was not tested?
* Does the normal firmware still compile?
* Does the dedicated plugin build compile?
* Does the production custom build compile?
* What flash, RAM, and LittleFS changes were measured?
* Is the source and included material license-compatible?

If AI helped create the plugin, that does not need a separate technical path. Run the same validation and provide the same evidence.

The remaining sections of this guide describe the exact implementation, patch-generation, dependency, validation, webapp, and review requirements.

# Technical Requirements

The previous sections explain which plugin path applies and the overall contribution workflow.

The sections below are the authoritative technical requirements. Follow only the sections relevant to your plugin type, plus the common validation and review requirements.

# Reference Implementations

Use existing implementations as references for specific architectural patterns:

* `plugins/default-web-apps/` — asset and webapp plugin reference.
* `plugins/grind-by-weight/` — built-in compile-gated feature reference.
* version-specific patch plugins — firmware-changing plugin reference.

Do not copy dependencies or implementation details merely because a reference plugin uses them.

# Webapp Files

Use the preferred layout:

```text
plugins/example/
  plugin.json
  webapp/
    index.html
    app.css
    app.js
```

Regular files under `webapp/` are staged at:

```text
/apps/<plugin-id>/
```

`index.html` identifies the app.

A non-default plugin may alternatively declare an ordinary asset targeting `index.html`. When that form is used, all declared assets belonging to the plugin form the app bundle and are staged under `/apps/<plugin-id>/`.

Do not use both forms in one plugin.

Use the `webapp/` form when the plugin also needs unrelated device-root assets.

Every webapp must resolve:

```text
littlefs
webserver
```

Declare:

```text
websocket
```

when the app uses `/snapshot` or another WebSocket-dependent API.

The build does not infer firmware API requirements from JavaScript.

Use relative paths for app resources:

```html
<link rel="stylesheet" href="./app.css">
<script src="./app.js"></script>
```

Use root-relative paths for firmware APIs:

```text
/setup/name
/snapshot
```

A plugin may reference files supplied by another plugin only when the dependency is declared explicitly.

For example:

```json
"depends_on": ["default-web-apps"]
```

The build reserves:

```text
/apps/
/webapps.json
```

Other assets must not claim those paths.

Effective staged targets must be unique across the complete selected plugin set.

Symlinks, path traversal outside the plugin directory, and invalid non-regular webapp entries are rejected.

# Device Root

When `default-web-apps` is selected, it keeps its dashboard at `/` and reads `/webapps.json` to discover other selected apps.

Without `default-web-apps`:

* one app redirects from `/` to that app;
* two or more apps produce a small launcher;
* no app serves the firmware setup page.

The canonical URL of an app remains:

```text
/apps/<plugin-id>/index.html
```

The generated registry has the form:

```json
{"schema":1,"apps":[{"id":"example","name":"Example","href":"/apps/example/index.html"}]}
```

Entries are sorted by plugin ID.

The app label comes from the plugin manifest's `name`.

All webapps share the device browser origin, storage, and firmware endpoints. `/apps/` is a file-ownership boundary, not a security sandbox.

Review app code for:

* firmware API calls;
* external requests;
* browser storage;
* destructive actions.

Use plugin-specific versioned storage keys, for example:

```text
openscale:example:settings:v1
```

# Configurator Presentation

A plugin may declare:

```json
"presentation": {
  "image": "media/preview.webp",
  "image_alt": "Example dashboard showing live scale weight",
  "handbook": "README.md"
}
```

Presentation paths are plugin-relative.

Preview images may be PNG, JPEG, or WebP and must remain within:

* 500 KiB;
* 4096 pixels per side;
* 16 megapixels.

An image requires non-empty alt text of at most 240 characters.

The handbook must be Markdown and at most 1 MiB.

SVG previews, PDF handbooks, and remote presentation URLs are not accepted.

Presentation content does not affect firmware build identity.

Runtime app files, effective asset paths and hashes, and the app label do.

Supply uncompressed runtime source assets. Do not provide:

```text
.html.gz
.js.gz
.css.gz
.svg.gz
```

The filesystem build generates deterministic compressed variants.

# Firmware Compile Gate

Every firmware-changing plugin must use one primary compile gate, for example:

```c
HDS_ENABLE_MY_PLUGIN
```

The applied patch must support:

```c
#ifndef HDS_ENABLE_MY_PLUGIN
#if HDS_CUSTOM_BUILD
#define HDS_ENABLE_MY_PLUGIN 1
#else
#define HDS_ENABLE_MY_PLUGIN 0
#endif
#endif
```

Guard every plugin-specific integration point, including where applicable:

* includes;
* globals;
* menu entries;
* display code;
* button handling;
* setup and loop hooks;
* storage;
* hardware initialization;
* networking;
* power behavior.

When disabled, the applied patch must not change normal firmware behavior or introduce missing symbols or unnecessary dependencies.

# Dedicated Build Environment

Every firmware patch plugin must provide:

```text
esp32s3-<plugin-id>
```

For example:

```ini
[env:esp32s3-my-plugin]
extends = env:esp32s3
build_flags =
  ${env:esp32s3.build_flags}
  -DHDS_ENABLE_MY_PLUGIN=1
```

Additional direct-validation compile gates may be added there when required.

Production feature requirements belong in `plugin.json`.

Do not add the plugin flag directly to the permanent `esp32s3-custom` environment.

# Firmware Architecture Rules

Follow existing firmware ownership rules.

Global firmware state belongs in the established firmware ownership model, including `include/parameter.h` where appropriate.

Shared state crossing tasks or asynchronous contexts requires real synchronization.

Do not use `volatile` or timing assumptions as substitutes for synchronization.

AsyncTCP callbacks must not perform:

* OLED work;
* I2C;
* SPI;
* power operations;
* blocking operations.

Queue or transfer the required state to an appropriate execution context.

Display helpers called from inside an active frame must not start another page loop.

Default BLE and USB behavior must remain available when WiFi is not selected.

New persistent settings require an isolated NVS namespace and documented migration behavior where applicable.

# Patch Generation

Generate the patch from the exact supported firmware revision and reviewed implementation commit:

```sh
git diff --binary <base-sha>...<implementation-sha> > plugins/my-plugin/patches/main.patch
```

Validate it with:

```sh
git apply --check --whitespace=error plugins/my-plugin/patches/main.patch
```

For every additional supported firmware release, generate and validate a separate patch against that release.

Do not relabel a `main` patch as release-compatible without applying and testing it on that release.

The catalog contribution may contain:

* the plugin package;
* generated catalogs;
* tests;
* required workflow changes;
* documentation.

It must not contain the applied implementation outside the plugin package.

# Dependency Semantics

Use `requires` for existing firmware features.

Examples:

```text
wifi
webserver
littlefs
websocket
```

Use `depends_on` for plugin dependencies.

Dependencies are transitive and dependency cycles are rejected.

Dependency patches are applied before their dependents.

Use `recommends` for complete combinations that maintainers have deliberately tested together.

Use `conflicts` for incompatible plugin IDs.

Use `conflicts_features` for incompatible feature IDs.

Feature conflicts remain strict after dependency resolution.

Plugin-to-plugin compatibility decisions inside an explicit dependency or recommended package follow the resolved package rules.

Do not use patch ordering as a compatibility mechanism. Overlapping patches should be redesigned.

The fact that the custom ZIP contains `littlefs.bin` does not imply that a plugin requires runtime `littlefs`.

OTA filesystem replacement and runtime filesystem access are separate mechanisms.

# Resource Budgets

Declare measured resource budgets with reasonable headroom:

```json
"budget": {
  "firmware_flash_bytes": 32768,
  "static_ram_bytes": 2048,
  "littlefs_bytes": 0
}
```

Do not use arbitrary maximum values.

Where useful, report both the measured change and declared budget.

# Validation

Create a temporary selection such as:

```json
{
  "firmware_ref": "main",
  "features": [],
  "plugins": ["my-plugin"]
}
```

Run:

```sh
python tools/configure_custom_build.py --catalog-output docs/custom-build/catalog.json --service-catalog-output docs/custom-build/service-catalog.json
python tools/test_plugin_catalog.py
python tools/test_custom_build_execution.py
python tools/test_plugin_ci_contract.py
python tools/test_grinder_feature_flag_contract.py
python tools/test_ai_docs_contract.py
pio run -e esp32s3
python tools/build_custom_firmware.py --config .pio.nosync/plugin-build.json --verify-plugin-environment esp32s3-my-plugin
python tools/build_custom_firmware.py --config .pio.nosync/plugin-build.json --output .pio.nosync/custom-output
```

The normal PlatformIO build verifies that the normal firmware still compiles.

The dedicated plugin verification applies the selected plugin and dependencies, runs matching plugin tests, and builds `esp32s3-<plugin-id>`.

The final command verifies the same package through the production custom-build compositor and produces the custom firmware output.

Pull-request CI performs equivalent validation for changed patch plugins and the firmware refs declared by those packages.

# Validation Evidence

Report validation according to what was actually performed.

Distinguish:

```text
Build validation
Device smoke testing
Peripheral/hardware validation
```

Do not infer physical behavior from a successful compile or source-level test.

For example, a build and menu smoke test do not establish sensor communication, calibration, electrical behavior, or radio coexistence.

Explicitly state unavailable hardware and untested cases.

# Review Submission

A submission should contain:

* the complete `plugins/<plugin-id>/` package;
* the implementation branch, commit, or PR used to generate each patch;
* test results for the normal build;
* direct plugin-environment results;
* production custom-build results;
* hardware results and explicit untested cases;
* license and provenance information;
* measured flash, static RAM, and LittleFS changes;
* regenerated browser and service catalogs.

Maintainers review:

* actual patch contents;
* architectural boundaries;
* security behavior;
* dependencies and conflicts;
* resource use;
* licensing and provenance;
* hardware claims.

