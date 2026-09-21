# Shot Flow for Half Decent Scale

The `shot_flow` web app charts espresso beverage flow, cumulative yield, and extraction time from the Half Decent Scale over the `/snapshot` WebSocket.

---

## Live Demo

The demo runs the production app with demo-only scale data and does not require hardware:

[https://decentespresso.github.io/openscale/shot-flow/shot_flow.html](https://decentespresso.github.io/openscale/shot-flow/shot_flow.html)

Demo data exists only in the Pages artifact and is not installed on the scale.

---

## Features

* Live, smoothed flow and cumulative-yield graph with automatic or manual shot control.
* Dose-adjusted Espresso, Turbo, saved-target, and free-pour modes.
* Dynamic graph bounds and optional grind guidance after a completed comparison.
* Worker-backed IndexedDB shot history, reusable profiles, deletion, and persistent defaults.
* DE1 legacy Tcl conversion through the repository tools.

---

## How to Use

1. Open **Shot Flow**, enter the dose, and select a target or **No target**.
2. Pull the shot using automatic detection or **Start shot**.
3. Review the graph and optional grind recommendation.
4. Use **Save shot** to retain actual samples and **Manage** for profiles, deletion, and defaults.

---

## Technical Details

* `main.js` requests 10 Hz snapshots and can send `tare`.
* `FlowEstimator` uses rolling linear regression and smoothing.
* Target yield is the integral of target flow, scaled to `dose * ratio`.
* Built-in curves cite published Decent Espresso profiles in the app.
* Targets use `openscale.shot-profile/v1`; see `docs/shot-flow-profile-format.md`.
* `storage-worker.js` owns IndexedDB stores for shots, profiles, and settings.

---

## Development Setup

Serve the complete assets directory so shared scripts resolve:

```bash
cd openscale/plugins/default-web-apps/assets
python -m http.server 8000
```

Open `http://localhost:8000/shot_flow/shot_flow.html`. A real `/snapshot` endpoint is required unless the Pages demo staging utility is used.

```bash
python tools/test_shot_flow_demo.py
node --test tools/test_shot_flow_model.mjs
```

---

## Code Structure Highlights

* `main.js`, `modules/shot-model.js`, and `modules/shot-chart.js`: live shot and graph logic.
* `modules/profile-tools.js`: profiles, records, and grind guidance.
* `storage-worker.js` and `modules/storage-client.js`: local persistence.
* `manage.html` and `manage.js`: data and settings management.

---

## Contributing

See the main [Programmer's Guide to the Half Decent Scale](https://decentespresso.com/docs/programmers_guide_to_the_half_decent_scale).

---

## License

This project is licensed under the **GNU General Public License v3.0**. See the repository `LICENSE` file.
