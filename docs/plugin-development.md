# Developing an OpenScale Plugin

## What a Plugin Is

OpenScale plugins are approved build inputs. Users select a plugin ID in the configurator; the build service never accepts arbitrary source URLs, uploaded ZIP files, or user-supplied patches.

A plugin package can contain:

- version-specific source patches;
- files staged into the runtime LittleFS image;
- dependency and conflict declarations;
- resource budgets and review metadata.

The selected package is applied only in a temporary checkout. Its implementation is not merged into the normal firmware source tree.

## Reference Implementations

Use both references, for different purposes:

- `plugins/hello-web/` shows an asset-only plugin with WiFi, WebServer, and runtime LittleFS dependencies.
- The package layout below defines version-specific firmware patches and their dedicated compile environments.

Grinder remains built into the firmware and is not converted into a patch package. It is the reference for containing an optional integration behind one compile gate. Study its environment, feature dependency checks, guarded state, menu integration, setup and loop hooks, storage, power behavior, and contract test. Do not copy its networking dependencies unless the new plugin actually needs them.

## Package Layout

```text
plugins/my-plugin/
|-- plugin.json
|-- patches/
|   |-- main.patch
|   `-- v3.2.0.patch
`-- assets/
    `-- page.html
```

Only `plugin.json` is mandatory. Patch and asset directories are optional.

Example manifest:

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
  "conflicts": [],
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

Use lowercase IDs containing letters, digits, and hyphens. Keep the plugin version independent from the firmware version. Budgets should include reasonable headroom over measured changes, not arbitrary maximum values.

## Build the Implementation First

Develop the feature on a separate implementation branch created from the exact supported firmware revision. Keep the normal firmware disabled by default and guard every integration point with one macro such as `HDS_ENABLE_MY_PLUGIN`.

The applied patch must support these modes:

```c
#ifndef HDS_ENABLE_MY_PLUGIN
#ifdef HDS_CUSTOM_BUILD
#define HDS_ENABLE_MY_PLUGIN 1
#else
#define HDS_ENABLE_MY_PLUGIN 0
#endif
#endif
```

Add a direct validation environment to the implementation patch:

```ini
[env:esp32s3-my-plugin]
extends = env:esp32s3
build_flags =
  ${env:esp32s3.build_flags}
  -DHDS_ENABLE_MY_PLUGIN=1
```

The environment name must be `esp32s3-<plugin-id>`. Put any additional compile gates needed by this plugin in that environment. Put custom-build feature requirements in `plugin.json` so the production compositor resolves them automatically.

Do not add the plugin flag directly to the repository's permanent `esp32s3-custom` environment. `HDS_CUSTOM_BUILD` enables the code only after the selected patch has been applied and avoids conflicts when several plugin patches are composed.

Follow the firmware ownership rules:

- global firmware state belongs in `include/parameter.h`;
- shared state crossing tasks needs real synchronization;
- AsyncTCP callbacks must not perform OLED, I2C, SPI, power, or blocking work;
- display helpers called inside a frame must not start another page loop;
- default BLE and USB behavior must remain available when WiFi is not selected;
- new persistent settings need an isolated NVS namespace and documented migration behavior when applicable.

## Create the Patch Package

Generate the patch from the exact base revision and the reviewed implementation commit:

```sh
git diff --binary <base-sha>...<implementation-sha> > plugins/my-plugin/patches/main.patch
```

The catalog branch should contain the package, generated catalogs, tests, workflow changes when required, and documentation. It should not contain the applied implementation outside `plugins/my-plugin/`.

Validate exact application:

```sh
git apply --check --whitespace=error plugins/my-plugin/patches/main.patch
```

For a release ref, generate a separate patch against that release and add both the ref and patch mapping to `plugin.json`. Never relabel a `main` patch as release-compatible without applying and testing it on that release.

## Declare Real Dependencies

Use `requires` for existing compile-time features:

- add `wifi` only when networking is used;
- add `webserver` only when the plugin registers HTTP behavior or serves a web UI;
- add `littlefs` only when runtime files are used;
- let declared features pull their existing transitive dependencies.

The custom ZIP always contains `littlefs.bin`. That does not mean the plugin requires runtime LittleFS. OTA replacement of the LittleFS partition and runtime filesystem use are separate mechanisms.

Use `conflicts` for plugin IDs that cannot safely be applied or operated together. Patch application order is deterministic by plugin ID, but overlapping patches should be redesigned rather than relying on order.

## Validate Before Review

Create a temporary selection file such as `.pio.nosync/plugin-build.json`:

```json
{
  "firmware_ref": "main",
  "features": [],
  "plugins": ["my-plugin"]
}
```

Run the focused checks:

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

The first PlatformIO build proves the normal firmware still compiles. The verification command applies exactly one patch plugin, runs matching `tools/test_my_plugin_*.py` files from the patch, and builds its dedicated environment. Pull-request CI performs this verification for every changed patch plugin and every firmware ref mapped by that package; it does not rebuild unrelated or asset-only plugins. The final command verifies the same package through the production `esp32s3-custom` compositor and creates the four-file ZIP.

Hardware testing must state what was actually available. A build and menu smoke test do not prove sensor communication, electrical behavior, calibration, or radio coexistence.

## Review Submission

A plugin submission should contain:

- the complete `plugins/<plugin-id>/` package;
- the implementation branch, commit, or open PR used to generate each patch;
- test results for the normal build, direct plugin environment, and production custom build;
- hardware results and explicit untested cases;
- license and provenance information;
- measured flash, static RAM, and LittleFS changes;
- regenerated browser and service catalogs.

Maintainers review the actual patch contents, security boundaries, dependencies, conflicts, licensing, and hardware claims before accepting the ID into the trusted catalog.

## Prompting an AI Contributor

Give the AI the feature source, plugin ID, supported firmware ref, available hardware, and this prompt:

```text
Work in decentespresso/openscale as a lazy senior developer.

First read AGENTS.md, docs/AI_REPO_MAP.md, docs/AI_PLUGIN_NOTES.md,
docs/AI_BUILD_NOTES.md, and docs/plugin-development.md. Inspect Grinder only as
the built-in compile-gating reference and plugins/hello-web as the asset-plugin
reference. Reuse existing build and catalog tools.

Turn <implementation PR, branch, or description> into the approved patch plugin
<plugin-id> for firmware ref <firmware-ref>. Keep the normal firmware source tree
unchanged outside plugins/<plugin-id>. Store implementation changes in a
version-specific patch, declare only real feature dependencies and conflicts,
and add no arbitrary download URL or uploaded-code path.

The applied patch must default its HDS_ENABLE_<PLUGIN> gate to HDS_CUSTOM_BUILD,
remain disabled in an applied normal esp32s3 build, and define the direct test
environment esp32s3-<plugin-id>. Guard every include, global, menu, display,
button, setup, loop, storage, and power integration. Keep WiFi and runtime
LittleFS optional unless the implementation actually uses them.

Regenerate both catalogs. Run patch validation, catalog and custom-build tests,
the normal esp32s3 build, the direct plugin-environment verification, and the
production custom build. Report measured resource changes and distinguish local
build, device smoke-test, and real peripheral validation. Do not commit, push,
merge, or close the source PR unless I explicitly ask.

Ask only when a dependency, hardware claim, license, or security decision cannot
be determined from the repository and supplied implementation.
```

Replace every placeholder before starting. For an existing implementation PR, keep it open until the patch build and available hardware checks have succeeded.
