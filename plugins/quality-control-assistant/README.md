# Quality Control Assistant

This temporary example plugin packages the on-device Quality Control Assistant as a namespaced webapp. It depends on `default-web-apps` for the dashboard, shared CSS, theme, and WebSocket client. Weigh Save and Dosing Assistant remain in `default-web-apps`.

The dashboard links directly to `/apps/quality-control-assistant/index.html` when this plugin is selected.

The app reads live weight from `/snapshot` and sends tare commands over the scale's WebSocket. It records pass/fail measurements against configurable thresholds, supports local presets and sound feedback, and exports CSV or JSON. Presets and measurements are stored in the browser's local storage for that scale's origin.

The app's HTML and JavaScript are in `webapp/`. Shared assets remain owned by `default-web-apps`; the plugin manifest declares that dependency rather than copying them. Firmware APIs use root-relative URLs, and app-local resources use paths relative to `webapp/index.html`.
