**Dosing Assistant for Decent Scales (WiFi)**

Use `dosing_assistant` to weigh repeated doses with container detection, target
thresholds, and measurement history. The bundled app connects to the serving
scale's `/snapshot` WebSocket; it does not use browser Bluetooth or USB Serial.
---

## Live Demo

The separately hosted Bluetooth variant is available here. Its connection
controls differ from the firmware-bundled WiFi app described below:

[https://decentespresso.com/support/scale/decentscale_dosingassistant](https://decentespresso.com/support/scale/samew_dosing_ast)

---

## Features

* **WiFi Connectivity:** Open the app from the scale's web page on the same network.
* **Configurable Dosing Settings:**
    * Set **target weight**
    * Define **overdose/underdose thresholds** enter your min or max limit for acceptable range of each dosing.
    * Optionally enable **audible feedback** for dosing results.
* **Real-time Weight Display:** Get live weight readings from your Half Decent Scale in grams.
* **Measurement Logging:** Each completed dosing sequence is logged with:
    * Timestamp
    * Final weight and overall result (Pass/Fail)
    * Target and low/high thresholds used for that reading.
* **Local Data Storage:** Completed readings are saved in this browser's local storage and restored on reload at the same origin. Clearing site data, private-browsing restrictions, or storage limits can remove or prevent persistence. Export important readings; a storage warning means the current readings must be exported before closing the tab.
* **Flexible Data Export:** Easily download your collected dosing data in:
    * **CSV (Comma Separated Values):** Great for spreadsheet analysis.
    * **JSON (JavaScript Object Notation):** Ideal for programmatic use or integration with other systems.
* **User Presets:** Save and load your specific dosing settings for different recipes or workflows, making setup quick and consistent.
* **No Frameworks:** Built purely with native JavaScript, HTML, and CSS for a lightweight and transparent codebase.

---

## How to Use

1.  **Ensure Scale is Ready:** Turn on your Half Decent Scale and connect it to WiFi.
2.  **Open in Browser:** Join the scale's network, open `http://hds.local` or its displayed IP address, then select Dosing Assistant.
3.  **Check Connection:** Wait for `WebSocket Connected`. No Bluetooth pairing or USB browser permission is needed.
4.  **Configure Dosing Settings:**
    * Define the **target weight**
    * Define **highThreshold/lowThreshold**
    * Use the "Object Name" and preset dropdown to **save or load your dosing configurations**.
    * Check "Enable Sounds" for audio cues.
    
5.  **Start Dosing:** Click **"Start"** to begin container setup.
6.  **Follow On-Screen Prompts:**
    * The app will prompt you for each stage after you click start.
    * First it will prompt you to **Put Container on the scale and click set container weight**. Setup starts dosing only if its tare command can be sent. If disconnected, reconnect and retry container setup explicitly; sending the command does not confirm completed hardware tare.
    * Now you can start dosing.
    * The app records the completed dose and waits for the container workflow before another reading.
    * Sounds indicate dosing results when enabled.
7.  **Export Data:** After completing a dosing sequence, the results are automatically logged. You can click **"CSV"** or **"JSON"** to download your complete dosing history.
8.  **Stop Dosing:** Click **"Stop"** to end the current sequence or cancel the initial container setup. A heavier replacement container prompts you to zero before continuing.

---

## Technical Details

The `dosing_assistant` application is built upon sophisticated browser APIs and a robust state management system:

* **WebSocket:** `main.js` connects to `ws://<page-host>/snapshot`. `modules/scale.js` receives weight readings and sends tare commands. Disconnects stop dosing; reconnect and restart explicitly. Use only a trusted network because the endpoint has no client authentication.
* **Dosing State Machine (`state-machine.js`):** The app uses these states:
    * `WAITING_FOR_NEXT`: Waiting to start a dosing cycle.
    * `MEASURING`: For measuring the object on scale.
    * `REMOVAL_PENDING`: After Measuring is done, it will prompt user to removal object from scale and put on the next one.
    * `CONTAINER_REMOVED`: A stage target if the container is removed or the weight goes below 0.
* **Weight Stability & Target Detection:** The app constantly monitors the weight stream, determining stability (`checkWeightStability`) and checking if each stage's target weight is met within its defined thresholds (`lowThreshold`, `highThreshold`).
* **Local Storage for Presets & Data:**
    * Dosing uses `decentScaleDosingPresets` with `{version: 1, presets: {...}}` and `lastUsedDosingPreset`. Before the first save, it reads compatible legacy entries and selections from `decentScalePresets` and `lastUsedPreset`. Saving copies them into the Dosing namespace without changing the legacy data or QC presets. Once the Dosing store exists, the app stops consulting either shared key. It ignores unsupported store versions and refuses to overwrite them.
    * Dosing history uses append-only `hds.dosing.history.v1.*` keys, one per reading, so simultaneous tabs do not overwrite each other's history. Reload combines saved readings and numbers the displayed rows. No measurement history existed to migrate from earlier versions.

---

## Development Setup

To explore, modify, or contribute to this application:

1.  **Download all files in this folder**.
2.  **Navigate to the folder** in your terminal:

    ```bash
    cd openscale/plugins/default-web-apps/assets/dosing_assistant
    ```

3.  **Serve the files locally over HTTP/HTTPS:**
    A local server lets you inspect layout, but the app tries `/snapshot` on
    that server's host. It needs a development proxy or an explicit
    development-only endpoint change to use a separate scale. For end-to-end
    use, serve it from the scale with its shared assets. A layout preview can
    use a local web server:

    **Using Python's `http.server` (Python 3, recommended):**

    ```bash
    python -m http.server 8000
    ```

    Then, open your Chrome or Edge browser and go to: `http://localhost:8000/`

    **Using Node.js `http-server` (if you have Node.js installed):**

    ```bash
    npm install -g http-server
    http-server -p 8000
    ```

    Then, open your Chrome or Edge browser and go to: `http://localhost:8000/`

---

## Code Structure Highlights

The core logic is modularized to handle different aspects of the dosing process:

* **`main.js`**: Connects the WebSocket and wires the dosing components to the page.
* **`modules/scale.js`**: Sends tare, handles weight readings, and manages container setup and dosing state.
* **`scale.js` / `state-machine.js`**: Contain dosing mode, weight evaluation, container weight handling, and dosing state transitions.
* **`ui-controller.js`**: Updates the DOM, status messages, input fields, and user interface events.
* **`state-machine.js`**: Handles state machine transition and state definition.
* **`presets.js`**: Manages user presets , save the user input date. 

---

## Contributing

We welcome community contributions! If you have suggestions, bug reports, or want to contribute code, 
please refer to the main [Programmers guide to the Half Decent Scale](https://decentespresso.com/docs/programmers_guide_to_the_half_decent_scale).
---

## License

This project is licensed under the **GNU General Public License v3.0**. See the `LICENSE` file in the main repository for more details.

---
