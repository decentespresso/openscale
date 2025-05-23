# Decent Scale Dosing Assistant Documentation

## Project Overview
The Decent Scale Dosing Assistant is a web application that interfaces with a Bluetooth-enabled scale to assist with precise dosing measurements. It provides real-time feedback, tracks measurement history, and allows data export. It is suitable for dosing multiple 18grams coffee beans, baking preparation or any other situation where you need to dose repeatly. 

## Module Structure

### Main Application (`main.js`)
**Purpose:** Entry point that initializes and connects all components.

**Key Functionality:**
- Initializes UI, state machine, and preset manager
- Sets up event listeners for buttons
- Configures export functionality

**Concerns:**
- Ensures proper initialization order of components
- Manages component dependencies

### Scale Interface (`modules/scale.js`)
**Purpose:** Handles communication with the Decent Scale via Bluetooth.

**Key Functionality:**
- Bluetooth device connection/disconnection
- Weight measurement and tare operations
- Dosing mode management
- Measurement recording and evaluation

**Concerns:**
- Bluetooth connection reliability
- Command sequencing and timing
- Weight data accuracy
- State management during dosing operations

### UI Controller (`modules/ui-controller.js`)
**Purpose:** Manages all user interface elements and interactions.

**Key Functionality:**
- Updates weight display and status indicators
- Manages progress bar visualization
- Displays measurement history
- Controls button states

**Concerns:**
- Responsive UI updates
- Clear user guidance
- Consistent visual feedback
- Proper display of measurement history (newest first)

### State Machine (`modules/state-machine.js`)
**Purpose:** Manages application state transitions during dosing operations.

**Key Functionality:**
- Defines and transitions between states (measuring, removal pending, etc.)
- Enforces valid state transitions
- Triggers appropriate UI updates based on state

**Concerns:**
- State consistency
- Proper transition validation
- Clear state-based user guidance

### Data Export (`modules/export.js`)
**Purpose:** Handles exporting measurement data to different formats.

**Key Functionality:**
- CSV export with appropriate headers and formatting
- JSON export with structured data
- File download handling

**Concerns:**
- Proper data formatting
- Consistent file naming
- Browser compatibility for downloads

### Preset Manager (`modules/presets.js`)
**Purpose:** Manages saved dosing presets.

**Key Functionality:**
- Saving and loading user presets
- Applying presets to current dosing settings

**Concerns:**
- Persistent storage of presets
- Validation of preset data

### Constants (`modules/constants.js`)
**Purpose:** Centralizes application constants.

**Key Functionality:**
- Defines Bluetooth characteristics
- Specifies state machine states
- Sets weight thresholds

**Concerns:**
- Consistent use throughout application
- Proper threshold values for accurate measurements

### Dosing Module (`modules/dosing.js`)
**Purpose:** Handles the core dosing logic.

**Key Functionality:**
- Measurement evaluation against thresholds
- Recording and formatting of measurement data
- Weight result will be announced via visual (color bar) and sound feedback to user. 
**Concerns:**
- Accurate threshold comparison
- Proper status determination
- Consistent data recording

## Data Flow

1. User connects to scale via Bluetooth
2. Scale readings are processed and displayed in real-time
3. When in dosing mode, measurements are evaluated against thresholds
4. Completed measurements are saved to history with pass and fail results. 
5. User can export history in CSV or JSON format

## Key Features

- Real-time weight display
- Visual progress indication
- Customizable target weights and thresholds
- Measurement history with status indicators
- Data export in multiple formats
- Preset management for common dosing targets

## Technical Implementation Notes

- Uses Web Bluetooth API for scale communication
- Implements a state machine pattern for dosing workflow
- Employs modular architecture for maintainability
- Uses Tailwind CSS for responsive styling
- Recommend to use this on Chrome, Edge brower on laptop and Andorid devices. Currently it is not supported by iOS devices. 
- To test locally , make sure you are at the file directory and run this command in terminal : npx http-server to host and access the web app in your browser. 
