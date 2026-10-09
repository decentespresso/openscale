# AI Web UI Notes

Read this for the default LittleFS web apps and their HDS styling.

## Theme Ownership

`docs/custom-build/index.html`, `styles.css`, and `app.js` are the visual and behavior reference. Do not refactor them as part of LittleFS app styling. The LittleFS dashboard, Weigh Save, Quality Control Assistant, and Dosing Assistant load `plugins/default-web-apps/assets/shared/theme.css` and `theme.js`. Quality Control Assistant lives in its own plugin and depends on those shared assets. The theme files are common assets for the device pages only.

The theme contract test compares the LittleFS light and dark tokens, brand mark, and animated toggle SVG with the configurator reference. It also checks each device page's theme references and the plugin manifest entries. `gzip_web_assets.py` makes deterministic gzip siblings for the LittleFS image; do not commit the generated `.gz` files.

The LittleFS script applies the theme from the document head before CSS renders and fills each `[data-hds-theme-toggle]` slot after the DOM is ready. It stores `{version: 1, preference}` at `hds-web-theme-v1`. On the same origin, it accepts the older `hds-custom-build-theme-v1` key if no valid new value exists. A missing preference follows `prefers-color-scheme`; pressing the toggle saves an explicit light or dark choice. The configurator keeps its original key and code. Browser storage is origin-specific, so a scale cannot read the docs site's preference.

The dashboard's inline CSS and the LittleFS `shared/app.css` own device page layouts. Reuse the theme tokens there instead of adding a second app-level stylesheet. Keep `include/webserver.h` and its embedded `HDS_WIFI_SETUP_PAGE` independent. That fallback works without LittleFS and must not depend on theme assets.

## Focused Checks

```sh
python tools/test_web_theme.py
python tools/test_plugin_catalog.py
python tools/test_ai_docs_contract.py
pio run -e esp32s3 -t buildfs
```

The Python contract checks token and SVG parity against the configurator, device HTML references and toggle slots, manifest entries, the inline setup page boundary, and theme preference behavior in Node. The custom firmware build workflow runs it for plugin changes. For visual review, inspect the four LittleFS pages on desktop and mobile in system, light, and dark modes; confirm the toggle, focus states, text contrast, header wrapping, and HDS scale interaction in a browser/device. `buildfs` validates packaging, not browser rendering or hardware behavior.
