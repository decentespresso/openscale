# AI Plugin Notes

Read this before creating, reviewing, or updating an approved compile-time plugin.

## Plugin Model

The normal firmware tree stays unchanged. An approved plugin is stored under `plugins/<plugin-id>/` as metadata, optional LittleFS assets, and version-specific patch files. `tools/build_custom_firmware.py` checks out an allowed firmware revision into a temporary directory, applies dependencies before their dependents, and builds there.

Only repository-owned plugin IDs are accepted. Never add uploaded archives, arbitrary URLs, external patch locations, or build-request-supplied code.

Three package forms are supported:

- Asset-only plugins add files below the staged LittleFS data directory. `plugins/default-web-apps/` is the reference.
- Patch plugins add compile-time firmware behavior through an approved package under `plugins/<plugin-id>/`.
- Built-in selector plugins expose an existing gated firmware feature in the plugin catalog without duplicating its source. `plugins/grind-by-weight/` is the reference.

## Required Package Shape

```text
plugins/<plugin-id>/
|-- plugin.json
|-- patches/
|   |-- main.patch
|   `-- <firmware-version>.patch
`-- assets/
```

`plugin.json` declares the stable ID, plugin version, compatible firmware refs, feature requirements, plugin dependencies, recommendations, conflicts, patch mapping, asset mapping, and resource budgets. A patch filename is not a compatibility claim by itself; it must be mapped by the manifest.

## Compile Gate Pattern

Patch plugins must be absent from an unmodified checkout and disabled when their applied source is built with the normal environment. Use one explicit gate across includes, state, setup, loop, display, menu, buttons, storage, and power behavior.

```c
#ifndef HDS_ENABLE_EXAMPLE
#ifdef HDS_CUSTOM_BUILD
#define HDS_ENABLE_EXAMPLE 1
#else
#define HDS_ENABLE_EXAMPLE 0
#endif
#endif
```

The patch must also add a direct validation environment named `esp32s3-<plugin-id>`:

```ini
[env:esp32s3-example]
extends = env:esp32s3
build_flags =
  ${env:esp32s3.build_flags}
  -DHDS_ENABLE_EXAMPLE=1
```

This gives three deliberate modes:

- `esp32s3` without the patch: normal firmware, no plugin source.
- `esp32s3-<plugin-id>` after applying the patch: isolated plugin validation.
- `esp32s3-custom` after applying the selected patch: production custom composition through `HDS_CUSTOM_BUILD`.

Do not make every plugin patch edit the `esp32s3-custom` block. The `HDS_CUSTOM_BUILD` default activates an applied patch without creating a shared patch-conflict hotspot in `platformio.ini`.

## Grind by weight Reference

Grind by weight is a built-in, repository-owned compile-time feature and is not a patch plugin. Its catalog package requires the existing internal `grinder` feature and contains no patch or asset. Keep the implementation in the source tree as the reference for a well-contained optional integration:

- `include/hds_features.h` owns its default, dependency checks, and compatibility alias.
- `platformio.ini` provides `esp32s3-grinder` as a direct validation environment.
- `#if HDS_ENABLE_GRINDER` guards all firmware integration points.
- global state lives in `include/parameter.h`.
- menu, setup, loop, power, and storage behavior remain inside the same gate.
- `tools/test_grinder_feature_flag_contract.py` prevents unguarded integration and stale dependencies.

Copy this containment pattern, not Grind by weight's WiFi requirements, implementation, or generated feature mapping. Patch plugin dependencies belong in `plugin.json`. Add a plugin to the core feature graph only when it is intentionally becoming a built-in firmware feature.

## Dependencies

Declare only dependencies exercised by the plugin:

- `wifi` for network access.
- `webserver` for HTTP routes or served web UI.
- `littlefs` for runtime filesystem files.
- other existing feature IDs only when their code is required.

Use `depends_on` for required plugin IDs. Dependency patches are applied before dependent patches, cycles are rejected, and the dependency order is part of the build identity. Use `recommends` only for a complete combination that has been tested together. The configurator's Recommended button replaces the current selection with that combination rather than retaining untested extras.

Do not infer WiFi, WebServer, or runtime LittleFS from the fact that a custom ZIP contains `littlefs.bin`. Runtime filesystem use and staged filesystem replacement are separate concerns. Pull OTA continues to use its mandatory staged `littlefs.bin` transaction independently of `HDS_FEATURE_LITTLEFS`.

## Version Compatibility

Every patch is generated against one allowed firmware ref and must pass `git apply --check --whitespace=error` on that ref. There is no fuzzy merge, three-way fallback, or automatic conflict resolution.

For a moving `main`, update `patches/main.patch` whenever upstream changes make it fail or invalidate its behavior. For a supported release, add a separate patch and manifest mapping for that exact release ref. Changing patch bytes, dependency declarations, the selected firmware commit, or the trusted builder commit changes the custom-build combination hash. Stable custom firmware reports `X.Y.Z-custom`; development builds retain their base suffix and append `-custom`.

## CI Contract

Patch-plugin pull requests compile the normal `esp32s3` environment and every changed patch plugin as `esp32s3-<plugin-id>`. The custom-build workflow derives a small matrix from plugin packages changed across the complete pull request and verifies every mapped firmware ref. Asset-only plugins are covered by the package and catalog contracts without an unnecessary firmware build. The plugin environment activates its own gate and any environment-level requirements. Do not add a permanent full feature matrix or duplicate the normal build in OTA contracts.

`tools/build_custom_firmware.py --verify-plugin-environment esp32s3-<plugin-id>` requires one selected target plugin, resolves its dependencies, enforces the environment naming convention, applies the trusted patches in an isolated checkout, runs matching `tools/test_<plugin-id>_*.py` files from the applied patch, and compiles the plugin environment.

Make each patch-plugin pull request run its own targeted environment. Do not grow an all-plugin matrix on every unrelated pull request.

## Focused Checks

```sh
python tools/test_plugin_catalog.py
python tools/test_custom_build_execution.py
python tools/test_plugin_ci_contract.py
python tools/test_grinder_feature_flag_contract.py
python tools/test_ai_docs_contract.py
git apply --check --whitespace=error plugins/<plugin-id>/patches/<ref>.patch
pio run -e esp32s3
python tools/build_custom_firmware.py --config <selection.json> --verify-plugin-environment esp32s3-<plugin-id>
python tools/build_custom_firmware.py --config <selection.json> --output .pio.nosync/custom-output
```

Hardware claims require matching hardware. Without it, report only build, boot, menu, button, transport, and no-crash coverage that was actually observed.
