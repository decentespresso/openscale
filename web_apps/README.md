## Half Decent Web Apps

Half Decent Web Apps Leveraging Low-Barrier Tech for Enhanced Scale Functionality. Half Decent Web Apps is a collection of three web applications built with plain JavaScript, HTML, and CSS, demonstrating various functionalities available through Web Bluetooth Low Energy (Web BLE) and Web USB.These apps aim to open up new ways to interact with your Half Decent Scale, providing powerful tools for quality control, precise weighing, and simplified dosing directly from your web browser.

***

### Experience the Apps: Live Demos

Get hands-on with our web tools. (Currently available for desktop/laptop browsers and Android devices. iOS is not supported at this time due to Web BLE/USB limitations on that platform.)

* **[Weigh and Save](https://decentespresso.com/support/scale/decentscale_webweigh)**: Your go-to app for general weighing tasks. Precisely measure items within a set timeframe and easily export the results for analysis or record-keeping.
* **[Quality Control Assistant](https://decentespresso.com/support/scale/decentscale_qcweigh)**: Perfect for ensuring consistency. This app allows you to repeatedly weigh the same items, simplifying data tracking and quality checks.
* **[Dosing Assistant](https://decentespresso.com/support/scale/samew_dosing_ast)**: Simplify your dosing process with an intuitive interface designed for achieving accurate and repeatable measurements.

***

### Getting Started: How to Connect & Use

Connecting your Half Decent Scale to our web apps is straightforward. Follow these steps:

1. **Browser Check**: Ensure you're using an up-to-date version of Google Chrome (version 136 or newer) or Microsoft Edge (version 136 or newer).
2. **Open an App**: Navigate to one of the app links provided above.
3. **Enable Pairing Mode**: Put your Half Decent Scale into Bluetooth pairing mode (refer to your scale's manual if needed).
4. **Connect via App**:
    * Click the "Connect" button within the web application.
    * A pop-up window will appear, listing available Bluetooth devices.
    * Select "Decent Scale" (or your scale's specific name) from the list.
5. **Connection Confirmed**: Once connected, the "Connect" button will turn red and change its text to "Disconnect." You will also see live weight updates from your scale displayed on the screen.
6. **Begin**: Click the "Start" button and follow the on-screen instructions specific to the app you're using.

**Using USB-C Connection (Optional for "Weigh and Save")**:
The "Weigh and Save" app also supports a direct USB-C connection. If you choose this method:

* You will need to install CH34X serial drivers on your device.
* Follow the [USB-C driver installation instructions here](https://decentespresso.com/docs/how_to_use_usbc_to_upgrade_the_firmware_on_your_half_decent_scale).

***

### Purpose & Audience

These web applications serve two main goals:

1. **For Scale Users**: To provide practical, easy-to-use tools that enhance the functionality of your Half Decent Scale for everyday tasks.
2. **For Developers**: To offer a real-world demonstration of Web Bluetooth and Web USB capabilities, encouraging exploration and innovation with these technologies.

**These apps are ideal for:**

* **Decent Espresso Machine and/or Half Decent Scale Owners**: Enhance your daily weighing and coffee preparation routines.
* **Quality Control Professionals**: Streamline weighing processes, improve consistency, and simplify data collection.
* **Developers & Tech Enthusiasts**: Explore a practical implementation of Web BLE and Web USB in an IoT context.

***

### For Developers: Technical Deep Dive

* **All codes are available at**[ openscale repo. ](https://github.com/decentespresso/openscale/tree/main/web_apps)
* **Foundation**: Built with standard HTML, CSS, and JavaScript.
* **Styling**: [Tailwind CSS](https://tailwindcss.com/) is used for a utility-first approach to styling, ensuring a responsive and modern interface. (Note: While Tailwind is a CSS framework, the core logic remains in plain JavaScript, avoiding heavy JS frameworks.)
* **Connectivity**:
    * **Web Bluetooth API (Web BLE)**: Enables wireless communication with the Half Decent Scale. [Learn more](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API).
    * **Web USB API**: Provides an alternative wired connection method. [Learn more](https://developer.mozilla.org/en-US/docs/Web/API/WebUSB_API).
    * *Browser Compatibility*: Natively supported in Chrome (version 80+) and Edge (version 80+). **Not currently supported on iOS devices.**
* **Data Storage**: Data such as readings and presets are stored locally in the browser's cache. Users can download their data as JSON or CSV files.

***

### Code Structure & Key Modules

The codebase is designed to be understandable and adaptable. "Weigh and Save" and "Dosing Assistant" feature a modular structure, while "Quality Control Assistant" uses a monolithic structure for a potentially simpler overview of function interactions.

Key JavaScript modules and their roles:

* `scale.js`: Handles BLE protocol, communication with the Half Decent Scale (HDS), and core dosing mode functionality.
* `constants.js`: Stores pre-programmed 10-byte messages for HDS communication and various threshold values.
* `state-machine.js`: Implements the core logic for "Dosing Assistant" and "Quality Control Assistant" using a Finite State Machine model.
* `export.js`: Manages the functionality for exporting weight readings and evaluation data as CSV or JSON files.
* `presets.js`: Allows users in "Dosing Assistant" and "Quality Control Assistant" to save target weights as presets, cached locally by the browser.
* `ui-controller.js`: Manages updates and changes to the HTML interface.
* `modules/connection/` (in "Weigh and Save"): Contains the specific implementations for BLE and USB connection methods.

For comprehensive details on the scale's communication protocols, refer to the [Programmer's Guide to the Half Decent Scale](https://decentespresso.com/docs/programmers_guide_to_the_half_decent_scale).

***

### Development Setup

To get a local copy up and running, follow these simple steps:

**Clone the repository:**

`git clone https://github.com/decentespresso/openscale/tree/main/web_apps/`

**Navigate into the project directory:**

<br>
`bash cd dosing_assistant`

<br>
**Serve the files with a local HTTP server:** :
Since these are web applications using Web BLE, they need to be served over https:// (or [http://localhost](http://localhost/)).

You can use a simple Python HTTP server or any other local server you prefer.
Using Python's http.server (Python 3):

`bash python -m http.server 8000`

Then, open your Chrome browser and navigate to
[http://localhost:8000/decentscale_webweigh/](http://localhost:8000/decentscale_webweigh/)

[http://localhost:8000/decentscale_qcweigh/](http://localhost:8000/decentscale_qcweigh/)

Using Node.js http-server (if you have Node.js installed):

`bash npm install -g http-server http-server -p 8000`

Then, navigate to the respective app URLs as above.

**This project is open source, and we encourage developers to explore, learn from, and contribute to the codebase.**
