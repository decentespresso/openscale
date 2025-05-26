# A Quality Control Weighing Application for Decent Scales

This web app is a web-based tool designed to streamline **quality control weighing processes**. Built with pure JavaScript, HTML, and CSS, it leverages **Web Bluetooth Low Energy (Web BLE)** to connect directly to your **Half Decent Scale**, providing a guided workflow for repetitive measurements, real-time feedback, and easy data export.

---

## Live Demo

Experience the app in action! It requires a **Chrome or Edge browser (version 70 or newer)** on a device with Bluetooth enabled:

[https://decentespresso.com/support/scale/decentscale_qcweigh](https://decentespresso.com/support/scale/decentscale_qcweigh)

---

## Features

* **Real-time Weight Display:** Get live weight readings from your Half Decent Scale in grams.
* **Guided QC Workflow:** A robust **state machine** guides you through the measurement process, prompting for object placement and removal.
* **Configurable Thresholds:** Define **goal weight**, **low threshold**, **high threshold**, and a **minimum weight** for object detection to customize pass/fail criteria.
* **Audible Feedback:** Optional sound alerts indicate whether a measurement **passed or failed**, allowing for quick, non-visual feedback.
* **Comprehensive Measurement Logging:** Every QC reading is saved with:
    * A unique reading ID
    * Timestamp
    * Measured weight
    * Pass/Fail result
    * The exact QC settings active at the time of measurement.
* **Local Data Storage:** All measurement data persists locally in your browser's cache (even if you close the tab, it will be there when you open it again).
* **Flexible Data Export:** Easily download your collected QC data in:
    * **CSV (Comma Separated Values):** Perfect for spreadsheet analysis.
    * **JSON (JavaScript Object Notation):** Ideal for programmatic use or integration with other systems.
* **User Presets:** Save and load your specific QC settings (e.g., for different coffee beans or products) to quickly switch between workflows. Presets are stored locally for convenience.
* **Fullscreen Mode:** Toggle full-screen view for a distraction-free experience.
* **No Frameworks:** Built purely with native JavaScript, HTML, and CSS for a lightweight and transparent codebase.

---

## How to Use

1.  **Open in Browser:** Navigate to the live demo link provided above using a Chrome or Edge browser (version 70 or newer) on a device with Bluetooth.
2.  **Ensure Scale is Ready:** Turn on your Half Decent Scale and make sure it's within Bluetooth range.
3.  **Connect to Scale:** Click the **"Connect to Scale"** button. A browser prompt will appear; select your "Decent Scale" from the list and click "Pair".
4.  **Configure Settings:**
    * Input your target **Goal Weight**, allowable **Low Threshold**, and **High Threshold**.
    * Set the **Min Weight** (the minimum weight detected to consider an object placed).
    * Check **"Enable Sounds"** if you want audio cues for pass/fail.
    * You can also save your current settings as a **preset** by typing a name in "Object Name" and selecting "Save Current Settings as Preset" from the dropdown. To load a saved preset, simply select it from the dropdown.
5.  **Start QC Mode:** Click the **"Start QC Mode"** button. The app will tare the scale and display status messages.
6.  **Follow On-Screen Prompts:**
    * "Place object on scale": Place your first item on the scale.
    * "Stabilizing...": The app waits for the weight to become stable.
    * "Measurement done, place next object": Once a stable reading is recorded and evaluated (Pass/Fail), a sound will play (if enabled), and you'll be prompted to remove the current item and place the next.
7.  **Export Data:** After completing your measurements, click **"Export to CSV"** or **"Export to JSON"** to download your collected data.
8.  **Stop QC Mode:** Click **"Stop QC Mode"** to end the current QC session.

---

## Technical Details

The `decentscale_qcweigh` application relies on a few key technical aspects:

* **Web BLE Communication:**
    * The app connects to the Half Decent Scale using its **service UUID `0000fff0-0000-1000-8000-00805f9b34fb`**.
    * **Read Characteristic:** `0000fff4-0000-1000-8000-00805f9b34fb` is used to receive weight data notifications.
    * **Write Characteristic:** `000036f5-0000-1000-8000-00805f9b34fb` is used to send commands like `tare` to the scale.
    * A **command queue (`commandQueue`)** ensures that commands sent to the scale are executed sequentially, preventing conflicts.
* **QC State Machine:** The core of the QC logic is managed by a state machine with three states:
    * **`WAITING_FOR_NEXT`**: The scale is waiting for an object to be placed.
    * **`MEASURING`**: An object has been detected, and the app is waiting for the weight to stabilize.
    * **`REMOVAL_PENDING`**: A measurement has been recorded, and the app is waiting for the object to be removed before prompting for the next.
* **Weight Stability:** Stability is determined by monitoring the change in weight over time. A `stabilityThreshold` (default **0.2g**) defines the maximum allowed fluctuation for a reading to be considered stable.
* **Local Storage for Presets:** User-defined QC settings are stored in `localStorage` under the key `'decentScalePresets'`, allowing them to persist across browser sessions. The `lastUsedPreset` is also stored.
* **Sound Generation:** Basic pass/fail sounds are generated directly in the browser using the **Web Audio API** (`AudioContext`, `OscillatorNode`, `GainNode`).
* **Fullscreen API:** Utilizes the HTML Fullscreen API (`requestFullscreen`, `exitFullscreen`) for a more immersive user interface.

---

## Development Setup

To explore, modify, or contribute to this application:

1.  **Download all files in this folder**.
2.  **Navigate to the folder** in your terminal:

    ```bash
    cd half-decent-web-apps/decentscale_qcweigh
    ```

3.  **Serve the files locally over HTTP/HTTPS:**
    Web BLE requires your application to be served over `https://` or `http://localhost`. You can use a simple local web server:

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

The core logic resides within the `DecentScale` class (`ble_jvs_QCv2.1.js`):

* **`constructor()`**: Initializes properties, sets up event listeners after DOM is loaded, and loads saved presets.
* **`initializeElements()` & `addEventListeners()`**: Handles linking JavaScript to HTML elements and attaching user interaction handlers.
* **`_findAddress()`, `_connect()`, `disconnect()`**: Manages the Web BLE connection lifecycle.
* **`notification_handler(event)`**: The primary event listener for incoming weight data, where the QC state machine logic and weight evaluation happen.
* **`toggleConnectDisconnect()`**: A utility function to switch between connecting and disconnecting.
* **`executeCommand(command)` & `_send(cmd)`**: Provides a queued approach to sending commands to the scale, improving reliability.
* **`_tare()` / `tare()`**: Implements the tare functionality for the scale.
* **`evaluateWeight(weight)`**: Simple logic to determine if a reading is "pass" or "fail" based on thresholds.
* **`playSound(type)`**: Generates audible feedback for pass/fail results.
* **`toggleQCmode()`, `startQCMode()`, `stopQCMode()`**: Controls the overall QC mode activation and deactivation.
* **`updateQCStatus(state, message)`**: Updates the user interface with current QC status messages and dynamic styling.
* **`saveMeasurement(weight, result)`**: Records each individual QC measurement, including its context.
* **`displayWeightReadings()`**: Updates the displayed list of past measurements and manages export button states.
* **`exportToCSV()` / `exportToJSON()` / `downloadFile()`**: Handles the logic for converting `weightData` into downloadable CSV and JSON files.
* **`setupPresetHandlers()`, `saveCurrentPreset()`, `savePreset()`, `getPresets()`, `getPreset()`, `loadPresets()`, `loadPreset()`, `updatePresetList()`**: A comprehensive set of methods for managing user-defined QC presets via `localStorage`.
* **`toggleFullScreen()`, `setupFullscreenHandler()`, `updateFullscreenState()`**: Manages the fullscreen browser functionality.

---

## Contributing

We welcome community contributions! If you have suggestions, bug reports, or want to contribute code, please refer to the main [Half Decent Web Apps repository's contribution guidelines](LINK_TO_MAIN_REPO_CONTRIBUTING.MD_HERE).

---

## License

This project is licensed under the **GNU General Public License v3.0**. See the `LICENSE` file in the main repository for more details.

---
