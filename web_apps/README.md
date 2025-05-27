**Half Decent Web Apps**
Leveraging Low-Barrier Tech for Enhanced Scale Functionality

Half Decent Web Apps is a collection of three web applications built with plain JavaScript, HTML, and CSS, demonstrating various functionalities available through Web Bluetooth Low Energy (Web BLE). These apps aim to open up new ways to interact with your Half Decent Scale, providing powerful tools for quality control, precise weighing, and simplified dosing directly from your web browser.

**Live Demos**
Experience the apps in action: (Available on laptop , Android devices. Not supported on iOS for now.)

**Weigh and Save** : (https://decentespresso.com/support/scale/decentscale_webweigh)

**Quality Control Assistant**: (https://decentespresso.com/support/scale/decentscale_qcweigh)

**Dosing Assistant**: (https://decentespresso.com/support/scale/samew_dosing_ast)


**Purpose & Audience**
The primary purpose of these applications is to showcase the diverse capabilities unlocked by connecting to the Half Decent Scale using readily accessible web technologies. They are designed for:
Decent Espresso machine and/or Half Decent Scale owners: To provide practical tools for daily use.
Quality control professionals: To streamline weighing processes and data collection.
Developers interested in Web BLE: To serve as a practical example of Web BLE implementation in a real-world IoT context.
Features
This repository contains three distinct web applications, each serving a unique purpose:

**decentscale_webweigh**: A Chrome web application for general weighing, allowing users to precisely measure items within a designated timeframe and export the results for further analysis.
**decentscale_qcweigh**: Designed for quality control, this web application enables users to repeatedly weigh the same items, facilitating consistency checks and data tracking.
**Dosing Assistant**: This web application aims to simplify the dosing process, providing an intuitive interface for achieving accurate and repeatable doses.
Technologies Used
These applications are built with a focus on simplicity and direct browser compatibility:

HTML, CSS, JavaScript: No external frameworks or libraries are used, keeping the codebase lean and easy to understand.

[Web Bluetooth API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API)

[Web USB API](https://developer.mozilla.org/en-US/docs/Web/API/WebUSB_API) 

2 primary connectivity methods to the Half Decent Scale are handled via the Web BLE standard and WEB USB, available natively in Chrome 70/Edge and later.
!!It is currently not supported on iOS devices.!!

Browser Cache: Data is stored locally in the browser's cache. Users have the option to download their data as JSON or CSV files for external use.
How to Use

To use these applications, simply visit the live demo links above with a compatible Chrome browser. Ensure your Half Decent Scale is turned on and discoverable. The applications will guide you through the connection process.

**Development Setup**
This project is open source, and we encourage developers to explore, learn from, and contribute to the codebase.

Running Locally
To get a local copy up and running, follow these simple steps:

Clone the repository:

Code snippet

git clone https://github.com/decentespresso/openscale/tree/main/web_apps/dosing_assistant

Navigate into the project directory:

Bash

cd dosing_assistant
Serve the files with a local HTTP server:
Since these are web applications using Web BLE, they need to be served over https:// (or http://localhost). You can use a simple Python HTTP server or any other local server you prefer.

Using Python's http.server (Python 3):

Bash

python -m http.server 8000
Then, open your Chrome browser and navigate to http://localhost:8000/decentscale_webweigh/, http://localhost:8000/decentscale_qcweigh/, or http://localhost:8000/samew_dosing_ast/.

Using Node.js http-server (if you have Node.js installed):

Bash

npm install -g http-server
http-server -p 8000
Then, navigate to the respective app URLs as above.

**Contributing**
We welcome community contributions! If you have suggestions, bug reports, or want to contribute code, please refer to the main [Programmers guide to the Half Decent Scale](https://decentespresso.com/docs/programmers_guide_to_the_half_decent_scale).
Open an issue to report bugs or suggest new features.
Fork the repository and submit a pull request with your changes.
License
This project is licensed under the GNU General Public License v3.0. See the LICENSE file for more details.

