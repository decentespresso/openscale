**Intelligent Dosing Assistant for Decent Scales (USB-C & Bluetooth)**

The `dosing_assistant` web application provides an advanced, guided workflow for precise, multi-stage weighing operations. 
---

## Live Demo

Experience the app in action! It requires a **Chrome or Edge browser (version 70 or newer)** on a device with Bluetooth.

[https://decentespresso.com/support/scale/decentscale_dosingassistant](https://decentespresso.com/support/scale/samew_dosing_ast)

---

## Features

* ** Connectivity:** Connect to your Half Decent Scale via :
    * **Wireless Bluetooth (Web BLE):** For convenience and portability.
* **Configurable Dosing Settings:**
    * Set **target weight**
    * Define **overdose/underdose thresholds** enter your min or max limit for acceptable range of each dosing.
    * Configure **stability thresholds** to determine when a reading is stable enough to proceed.
    * Adjust **pre-wet/wait times** between stages.
    * Optionally enable **audible feedback** for stage transitions and completion.
* **Real-time Weight Display:** Get live weight readings from your Half Decent Scale in grams.
* **Measurement Logging:** Each completed dosing sequence is logged with:
    * Timestamp
    * Final weight and overall result (Pass/Fail)
    * Detailed breakdown of each stage's target, actual weight, and individual result.
* **Local Data Storage:** All logged dosing sessions persist locally in your browser's cache.
* **Flexible Data Export:** Easily download your collected dosing data in:
    * **CSV (Comma Separated Values):** Great for spreadsheet analysis.
    * **JSON (JavaScript Object Notation):** Ideal for programmatic use or integration with other systems.
* **User Presets:** Save and load your specific dosing settings for different recipes or workflows, making setup quick and consistent.
* **No Frameworks:** Built purely with native JavaScript, HTML, and CSS for a lightweight and transparent codebase.

---

## How to Use

1.  **Open in Browser:** Navigate to the live demo link provided above using a Chrome or Edge browser (version 70 or newer) on a device with Bluetooth or a USB-C port.
2.  **Ensure Scale is Ready:** Turn on your Half Decent Scale.
3.  **Choose Connection Method:**
    * **For Bluetooth:** Ensure your scale is within Bluetooth range. Click the **"Connect via Bluetooth"** button. A browser prompt will appear; select your "Decent Scale" from the list and click "Pair".
4.  **Configure Dosing Settings:**
    * Define the **target weight**
    * Define **highThreshold/lowThreshold**
    * Enter **Preset Name**
    * Use the "Object Name" and preset dropdown to **save or load your dosing configurations**.
    * Check "Enable Sounds" for audio cues.
    
5.  **Start Dosing:** Click the **"Start"** button. The app will tare the scale and guide you through the defined stages.
6.  **Follow On-Screen Prompts:**
    * The app will prompt you for each stage after you click start.
    * First it will prompt you to **Put Container on the scale and click set container weight**, after click it will tare the scale.
    * Now you can start dosing.
    * It will detect when the target weight for a stage is reached and stabilized before moving to the next.
    * Sounds will indicate completion of a stage or errors.
7.  **Export Data:** After completing a dosing sequence, the results are automatically logged. You can click **"CSV"** or **"JSON"** to download your complete dosing history.
8.  **Stop Dosing:** Click **"Stop Dosing"** to end the current sequence or reset the process.

---

## Technical Details

The `dosing_assistant` application is built upon sophisticated browser APIs and a robust state management system:

* **Web BLE :** Provides connectivity options, allowing the user to choose the preferred method for interacting with the scale. The app dynamically switches between the `ble_controller.js` and `serial_controller.js` logic based on user selection.
* **Dosing State Machine (`state-machine.js`):** This is the core of the app, managing the sequential flow of the dosing process. States likely include:
    * `WAITING_FOR_NEXT`: Waiting to start a dosing cycle.
    * `MEASURING`: For measuring the object on scale.
    * `REMOVAL_PENDING`: After Measuring is done, it will prompt user to removal object from scale and put on the next one.
    * `CONTAINER_REMOVED`: A stage target if the container is removed or the weight goes below 0.
* **Weight Stability & Target Detection:** The app constantly monitors the weight stream, determining stability (`checkWeightStability`) and checking if each stage's target weight is met within its defined thresholds (`lowThreshold`, `highThreshold`).
* **Command Queue:** A robust command queue ensures reliable communication with the scale, preventing commands from being dropped or interfering with each other.
* **Local Storage for Presets & Data:**
    * Dosing presets are stored in `localStorage` under keys like `'decentScaleDosingPresets'`, ensuring user configurations persist.
    * Dosing history is also saved locally for later retrieval and export.

---

## Development Setup

To explore, modify, or contribute to this application:

1.  **Download all files in this folder**.
2.  **Navigate to the folder** in your terminal:

    ```bash
    cd openscale/web_apps/dosing_assistant
    ```

3.  **Serve the files locally over HTTP/HTTPS:**
    Web BLE and Web Serial API both require your application to be served over `https://` or `http://localhost`. You can use a simple local web server:

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

* **`main.js`**: Likely the entry point, coordinating the different components, managing UI, and handling overall application state.
* **`scale.js`**: Encapsulates all Web Bluetooth API interactions, including device discovery, connection, characteristic operations, and more.
* **`dosing.js`**: Contains the dosing mode, evaluate weight against target, container weight , enter and exit the dosing mode.
* **`ui_controller.js`**: Responsible for updating the DOM, displaying status messages, managing input fields, and handling user interface events.
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
