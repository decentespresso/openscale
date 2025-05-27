**Simple Weigh & Save Web App for Decent Scales (USB-C & Bluetooth)**

This compact web application provides a straightforward way to read weights from your **Half Decent Scale** and record them. Built with pure JavaScript, HTML, and CSS, it offers **flexible connectivity options** via both **Web Bluetooth Low Energy (Web BLE)** and **Web Serial API (USB-C)**. This allows you to easily log multiple measurements and export your data for future use.

---

## Live Demo

Experience the app in action! It requires a **Chrome or Edge browser (version 70 or newer)** on a device with Bluetooth or a USB-C port (for serial connection):

[https://decentespresso.com/support/scale/decentscale_weighandsave](https://decentespresso.com/support/scale/decentscale_weighandsave)

---

## Features

* **Dual Connectivity:** Connect to your Half Decent Scale via either:
    * **Wireless Bluetooth (Web BLE):** For convenience and portability.
    * **Wired USB-C (Web Serial API):** For a potentially more stable and faster connection.
* **Real-time Weight Display:** Get live weight readings from your Half Decent Scale in grams.
* **Manual Measurement Saving:** Click a button to save the current stable weight reading.
* **Measurement Logging:** Each saved reading is logged with:
    * A timestamp
    * The measured weight
* **Local Data Storage:** Measuremenet is saved locally, make sure you download before you reload the page, otherwise it won't save.
* **Flexible Data Export:** Easily download your collected data in:
    * **CSV (Comma Separated Values):** Great for spreadsheet analysis.
    * **JSON (JavaScript Object Notation):** Ideal for programmatic use or integration with other systems.
* **No Frameworks:** Built purely with native JavaScript, HTML, and CSS for a lightweight and transparent codebase.

---

## How to Use

1.  **Open in Browser:** Navigate to the live demo link provided above using a Chrome or Edge browser (version 70 or newer) on a device with Bluetooth or a USB-C port.
2.  **Ensure Scale is Ready:** Turn on your Half Decent Scale.
3.  **Choose Connection Method:**
    * **For Bluetooth:** Ensure your scale is within Bluetooth range. Click the **"Connect via Bluetooth"** button. A browser prompt will appear; select your "Decent Scale" from the list and click "Pair".
    * **For USB-C:** Connect your scale to your computer via a USB-C cable. Click the **"Connect via USB-C"** button. A browser prompt will appear; select your "Decent Scale" serial port from the list and click "Connect".
4.  **Weigh and Save:**
    * Place an object on the scale.
    * Once the weight stabilizes, click the **"Save Reading"** button. The reading will be added to the history list below.
5.  **Timer:** Once you click start it will record weight at each interval , and calculate rate per each interval. 
6.  **Export Data:** After collecting your measurements, click **"CSV"** or **"JSON"** to download your collected data.
7.  **Zero Scale:** Use the **"Zero Scale"** button to tare the scale at any time.

---

## Technical Details

The `weigh_save` application leverages advanced browser APIs for hardware interaction:

* **Web BLE Communication:**
    * The app connects to the Half Decent Scale using its **service UUID `0000fff0-0000-1000-8000-00805f9b34fb`**.
    * **Read Characteristic:** `0000fff4-0000-1000-8000-00805f9b34fb` is used to receive weight data notifications.
    * **Write Characteristic:** `000036f5-0000-1000-8000-00805f9b34fb` is used to send commands like `tare` to the scale.
    * A **command queue (`commandQueue`)** ensures that commands sent to the scale are executed sequentially.
* **Web Serial API Communication (USB-C):**
    * Utilizes `navigator.serial` to open a serial port connection to the Decent Scale when connected via USB-C.
    * Reads incoming data from the serial port to get weight readings.
    * Sends commands (like tare) over the serial port.
* **Weight Stability:** Readings are typically saved when the weight is stable, determined by minimal fluctuation over a short period.
* **Local Storage for Data:** All collected readings are stored in `localStorage` under the key `'decentScaleWeighSaveData'`, allowing them to persist across browser sessions.
* **Data Export:** The app dynamically generates CSV and JSON files from the stored data for download.

---

## Development Setup

To explore, modify, or contribute to this application:

1.  **Download all files in this folder**.
2.  **Navigate to the folder** in your terminal:

    ```bash
    cd openscale/web_apps/Weigh_Save
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

The core logic is likely split across files to handle different connection methods, but the overall structure would include:

* **`DecentScale` Class (or similar):** A central class managing the scale's state, data handling, and UI updates.
* **Bluetooth-specific JS file (e.g., `ble_jvs_weigh_save.js`):** Handles Web BLE connection, characteristic reading/writing, and notification handling.
* **Serial-specific JS file (e.g., `serial_api_weigh_save.js`):** Handles Web Serial API connection, reading data from the serial port, and writing commands.
* **Shared logic:** Functions for saving/loading data to `localStorage`, displaying readings, and exporting data.

---

## Contributing

We welcome community contributions! If you have suggestions, bug reports, or want to contribute code, please refer to our [Programmers guide to the Half Decent Scale](Programmers guide to the Half Decent Scale)

---

## License

This project is licensed under the **GNU General Public License v3.0**. See the `LICENSE` file in the main repository for more details.

---
