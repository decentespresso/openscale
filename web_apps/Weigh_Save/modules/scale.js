import { SCALE_CONSTANTS } from './constants.js';
import { StateMachine } from './state-machine.js';
import { UIController } from './ui-controller.js';
import { DataExport } from './export.js';
import { BluetoothConnection } from './connections/bluetooth-connection.js';
import { USBConnection } from './connections/usb-connection.js';
import { DebugLogger as Debug } from './debug-logger.js';

export class DecentScale {
    constructor(uiController, timerManager) {
        this.uiController = uiController;
        this.timerManager = timerManager;
        
        // Scale properties
        this.device = null;
        this.server = null;
        this.readCharacteristic = null;
        this.writeCharacteristic = null;
        this.timeout = 20;
        this.fixDroppedCommand = true;
        this.CHAR_READ = SCALE_CONSTANTS.CHAR_READ;
        this.CHAR_WRITE = SCALE_CONSTANTS.CHAR_WRITE;
        this.ledOn = false;
        this.weightReadings = [];
        this.weightData = [];
        this.commandQueue = Promise.resolve();
        
        // Container and dosing properties
        this.containerWeight = 0;
        this.waitingForContainerWeight = false;
        this.dosingPausedForContainerRemoval = false;
        this.highThreshold = 0;
        this.lowThreshold = 0;
        this.dosingMode = false;
        this.tareWeight = 0;
        this.currentProgress = 0;
        this.lastWeight = 0;
        this.doseSaved = false;
        
        // Stability tracking
        this.stableWeightReadings = [];
        this.stabilityThreshold = 0.2;
        this.stabilityDuration = 4;
        this.doseCompletedAndSaved = false;
        this.containerReplacedWeightReadings = [];
        this.containerReplacementStabilityCheckTimeout = null;
        this.autoRestartDoseSaved = false;
        // State machine
        this.stateMachine = new StateMachine(uiController);
        // Reading counter
        this.readingCount = 0;
        this.soundplayed = false;
        //rate counter
        this.lastWeightTime=0;
        //new connection type 
        this.connection = null; // Will hold the connection instance
        this.connectionType = null; // BLE or USB, can be set by UI
        // Initialize connections
        this.btConnection = new BluetoothConnection(uiController);
        this.usbConnection = new USBConnection(uiController);
        this.activeConnection = null;
                // Set notification handler
        this.btConnection.setNotificationHandler(this.notification_handler.bind(this));
        this.usbConnection.setNotificationHandler(this.notification_handler.bind(this));
    }
//BLE OR USB connect method 
async connect(type) {
    if (!type) {
        Debug.log('SCALE', 'No connection type specified');
        return false;
    }

    Debug.log('SCALE', `Connecting using ${type}`);
    
    if (this.activeConnection) {
        await this.disconnect();
    }

    // Set connection type
    this.connectionType = type;
    
    // Only try the specified connection type
    try {
        if (type === 'usb') {
            this.activeConnection = this.usbConnection;
        } else if (type === 'bluetooth') {
            this.activeConnection = this.btConnection;
        } else {
            Debug.log('SCALE', `Invalid connection type: ${type}`);
            return false;
        }

        const connected = await this.activeConnection.connect();
        Debug.log('SCALE', `Connection result: ${connected}`);
        
        if (!connected) {
            this.activeConnection = null;
            this.connectionType = null;
            return false;
        }

        return true;

    } catch (error) {
        Debug.log('SCALE', 'Connection error:', error);
        this.activeConnection = null;
        this.connectionType = null;
        return false;
    }
}

    async _findAddress() {
        try {
            const device = await navigator.bluetooth.requestDevice({
                filters: [{ name: 'Decent Scale' }],
                optionalServices: [this.CHAR_READ]
            });
            return device;
        } catch (error) {
            console.error('Find address error:', error);
            return null;
        }
    }

    async _connect(device) {
        try {
            this.server = await device.gatt.connect();
            const service = await this.server.getPrimaryService(this.CHAR_READ);
            
            this.readCharacteristic = await service.getCharacteristic(SCALE_CONSTANTS.READ_CHARACTERISTIC);
            this.writeCharacteristic = await service.getCharacteristic(SCALE_CONSTANTS.WRITE_CHARACTERISTIC);
            
            await this._enableNotification();
        } catch (error) {
            console.error('Connect error:', error);
            throw error;
        }
    }

    async disconnect() {
        if (this.activeConnection) {
            Debug.log('SCALE', 'disconnect called');
            await this.activeConnection.disconnect();
            this.activeConnection = null;
            this.connectionType = null;
            this.uiController.updateConnectButton('Connect to Scale');
        }
    }

    async _disconnect() {
        if (this.server) {
            await this._disable_notification();
            this.server.disconnect();
        }
        this.device = null;
        this.server = null;
        this.readCharacteristic = null;
        this.writeCharacteristic = null;
        this.uiController.updateStatus('Not connected');
    }

    async _enableNotification() {
        await this.readCharacteristic.startNotifications();
        this.readCharacteristic.addEventListener('characteristicvaluechanged', this.notification_handler.bind(this));
    }

    async _disable_notification() {
        if (this.readCharacteristic) {
            await this.readCharacteristic.stopNotifications();
            this.readCharacteristic.removeEventListener('characteristicvaluechanged', this.notification_handler.bind(this));
        }
    }

    // Command execution
    async executeCommand(command) {
        return new Promise(async (resolve, reject) => {
            try {
                await this.commandQueue;
                this.commandQueue = command();
                const result = await this.commandQueue;
                resolve(result);
            } catch (error) {
                console.error('Command execution error:', error);
                reject(error);
            }
        });
    }

    async _send(cmd) {
        if (!this.activeConnection) {
            throw new Error('Device not connected');
        }
        await this.activeConnection.sendCommand(new Uint8Array(cmd));
    }

    async tare() {
        await this._tare();
    }

    async _tare() {
        await this._send([0x03, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x0C]);
        console.log("Tare requested.");
    }

    // Notification handler
    notification_handler(event) {
        try {
            const data = event.target.value;
            if (data.getUint8(0) !== 0x03 || data.byteLength !== 7) return;

            const type = data.getUint8(1);
            if (type === 0xCE || type === 0xCA) {
                const weight = data.getInt16(2, false) / 10;
                this.uiController.updateWeightDisplay(weight);
                
                if (this.timerManager && this.timerManager.shouldTakeMeasurement()) {
                    const currentTime = Date.now();
                    const weight_tole = Math.abs(weight - this.lastWeight); // Use absolute value
                    console.log("weight_tole", weight_tole);
                    
                    // Take measurement if weight change exceeds threshold in either direction
                    if (weight_tole >= this.stabilityThreshold && weight !== this.lastWeight&& Math.abs(weight)>this.stabilityThreshold) {
                        this.readingCount++;
                        const rate = this.rate_grams_per_second(weight, currentTime);
                        this.uiController.updateRateDisplay(rate);
                        
                        const reading = {
                            readings: this.readingCount,
                            timestamp: new Date().toLocaleString('en-UK', {
                                year: 'numeric',
                                month: '2-digit',
                                day: '2-digit',
                                hour: '2-digit',
                                minute: '2-digit',
                                second: '2-digit'
                            }),
                            weight: weight.toFixed(1),
                            elapsedTime: this.timerManager.elapsedTime,
                            rate: rate.toFixed(1)
                        };

                        // Update last values for next calculation
                        this.lastWeight = weight;
                        this.lastWeightTime = currentTime;

                        // Add the reading to both arrays
                        this.weightData.push(reading);
                        this.weightReadings.push(
                            `${this.readingCount}, ${reading.weight}g, ${rate.toFixed(1)}g/s, ${reading.timestamp}`
                        );

                        this.uiController.displayWeightReadings(this.weightReadings);
                        this.uiController.updateExportButtonStates(true);
                        
                        console.log("Measurement taken:", reading);
                    }
                }
            }
        } catch (error) {
            console.error('Weight update error:', error);
        }
    }

    handleDosingMode(netWeight) {
        const target = this.dosingSettings?.targetWeight || 0;
        const lowThreshold = this.dosingSettings?.lowThreshold || 0;
        const highThreshold = this.dosingSettings?.highThreshold || 0;

        // Check if dosingSettings exists
        if (!this.dosingSettings) {
            console.error('Dosing settings not configured');
            return;
        }

        // Calculate progress percentage
        this.currentProgress = Math.min((netWeight / target) * 100, 100);
        console.log('Progress:', `${this.currentProgress.toFixed(1)}%`);
        
        // Update progress bar and percentage display
        this.uiController.updateProgressBar(this.currentProgress);
        
        // Update progress bar color based on weight
        if (netWeight >= lowThreshold && netWeight <= highThreshold) {
            this.uiController.updateProgressBarColor('success');
        } else if (netWeight < lowThreshold) {
            this.uiController.updateProgressBarColor('warning');
        } else if (netWeight > highThreshold) {
            this.uiController.updateProgressBarColor('error');
        }

        // Let state machine handle the weight update
        this.stateMachine.handleWeightUpdate(netWeight, this);
    }

    // Dosing methods
    toggleDosingMode() {
        if (this.dosingMode) {
            // If currently in dosing mode, stop it
            console.log('Stopping dosing mode...');
            this.dosingMode = false; // Set this first
            
            this.uiController.updateGuidance('Dosing Stopped');
            this.uiController.updateDosingButton('Start', false);
            
            // Hide and disable the Set Container Weight button
            this.uiController.hideSetContainerWeightButton();
            this.uiController.disableSetContainerWeightButton();
            
            this.uiController.updateProgressBarColor('default');
            this.currentProgress = 0;
            this.doseSaved = false; // Reset the saved flag
            this.stopDosing(true); // Call stopDosing(true) for manual stop
        } else {
            // If not in dosing mode, start it
            console.log('Starting dosing mode setup...');
            this.doseSaved = false; // Reset the saved flag when starting new dosing
            this.doseCompletedAndSaved = false;
            this.currentProgress = 0;
            this.stableWeightReadings = [];

            // Update button text first
            this.uiController.updateDosingButton('Stop', true);
            this.uiController.updateGuidance('Place container on scale and click set container weight.');
            
            // Show and enable the Set Container Weight button
            this.uiController.showSetContainerWeightButton();
            this.uiController.enableSetContainerWeightButton();
            
            // Set states last
            this.waitingForContainerWeight = true;
            console.log('Dosing mode setup complete:', {
                dosingMode: this.dosingMode,
                waitingForContainer: this.waitingForContainerWeight,
                buttonVisible: true
            });
        }
    }

    startDosingAutomatically() {
        if (!this.dosingMode) {
            console.log('Auto-Restarting Dosing Mode...');
            this.dosingMode = true;
            this.doseCompletedAndSaved = false;
            this.currentProgress = 0;
            
            // Reset stability tracking
            this.stableWeightReadings = [];
            this.doseSaved = false;
            
            // Update UI
            this.uiController.updateProgressBar(0);
            this.uiController.updateProgressBarColor('default');
            this.uiController.updateDosingButton('Stop', true);
            this.uiController.updateGuidance('Dosing automatically resumed. Ready for next measurement.', 'success');
            
            // Set state
            this.stateMachine.setCurrentState(SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT);
            
            console.log('Dosing mode auto-restarted with settings:', {
                targetWeight: this.dosingSettings.targetWeight,
                lowThreshold: this.dosingSettings.lowThreshold,
                highThreshold: this.dosingSettings.highThreshold
            });
        }
    }

    stopDosing(manualStop = true) {
        this.dosingMode = false;
        this.currentProgress = 0;
        console.log('Dosing mode stopped:', manualStop ? 'manual stop' : 'automatic stop');
        
        // Only reset container removal flag if it's a manual stop
        if (manualStop) {
            this.dosingPausedForContainerRemoval = false;
        }
        
        // Update UI
        this.uiController.updateProgressBar(0);
        this.uiController.updateDosingButton('Start', false);
        this.uiController.updateGuidance(manualStop ? 'Ready to start' : 'Container removed, will auto-restart when replaced');
        this.uiController.disableSetContainerWeightButton();
        this.uiController.updateProgressBarColor('default');
        
        this.waitingForContainerWeight = false;
    }

    setContainerWeight() {
        const currentWeight = parseFloat(this.uiController.getWeightDisplay());
        
        if (isNaN(currentWeight) || currentWeight < 0) {
            alert('Please place a container on the scale');
            return;
        }
        
        this.containerWeight = currentWeight;
        this.tareWeight = currentWeight;
        
        this.uiController.updateGuidance('Container weight set. Enter target weight and thresholds.');
        this.uiController.enableStartDosingButton(); // This line is now fixed
        this.uiController.disableSetContainerWeightButton();
        
        this.configureDosingSettings();
        this.waitingForContainerWeight = false;
    }

    configureDosingSettings() {
        // Get values from input fields
        const targetWeight = parseFloat(document.getElementById('targetWeight').value);
        const lowThreshold = parseFloat(document.getElementById('lowThreshold').value);
        const highThreshold = parseFloat(document.getElementById('highThreshold').value);
        
        if (isNaN(targetWeight) || targetWeight <= 0) {
            alert('Please enter a valid target weight');
            return;
        }
        
        if (isNaN(lowThreshold) || lowThreshold < 0) {
            alert('Please enter a valid low threshold');
            return;
        }
        
        if (isNaN(highThreshold) || highThreshold <= lowThreshold) {
            alert('Please enter a valid high threshold (must be greater than low threshold)');
            return;
        }
        
        this.dosingSettings = {
            targetWeight,
            lowThreshold,
            highThreshold
        };
        console.log("dosing settings", this.dosingSettings);
        
        this.tare();
        this.tareWeight = 0;
        
        // Set dosing mode to active
        this.dosingMode = true;
        this.stateMachine.setCurrentState(SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT);
        this.uiController.updateDosingButton('Stop', true);
        this.uiController.updateGuidance('Ready! Place object on scale');
    }

    saveDosing(finalWeight) {
        this.readingCount++;
        const now = new Date();
        const reading = {
            readings: this.readingCount,
            timestamp: now.toLocaleString('en-GB'),
            weight: finalWeight.toFixed(1),
            target: this.dosingSettings.targetWeight,
            lowThreshold: this.dosingSettings.lowThreshold,
            highThreshold: this.dosingSettings.highThreshold,
            status: this.getDoseStatus(finalWeight)
        };

        this.weightData.push(reading);
        this.weightReadings.push(
            `${this.readingCount}. ${reading.timestamp}: ${reading.weight}g / ${reading.target}g - ${reading.status}`
        );
        
        this.uiController.displayWeightReadings(this.weightReadings);
        console.log('Measurement saved:', reading);
    }

    getDoseStatus(weight) {
        const target = this.dosingSettings.targetWeight;
        const lowThreshold = this.dosingSettings.lowThreshold;
        const highThreshold = this.dosingSettings.highThreshold;
        
        if (weight < lowThreshold) {
            return 'FAIL_LOW';
        } else if (weight > highThreshold) {
            return 'FAIL_HIGH';
        } else if (weight >= lowThreshold && weight <= highThreshold) {
            if (weight === target) {
                return 'OK';
            } else {
                return 'PASS';
            }
        } else {
            return 'UNKNOWN';
        }
    }

    isWeightStable() {
        if (this.stableWeightReadings.length < this.stabilityDuration) {
            return false;
        }
        
        const maxWeight = Math.max(...this.stableWeightReadings);
        const minWeight = Math.min(...this.stableWeightReadings);
        const weightDifference = maxWeight - minWeight;
        
        return weightDifference <= this.stabilityThreshold;
    }

    playSound(type) {
        if (this.soundplayed) return;
        
        const audioContext = new (window.AudioContext || window.webkitAudioContext)();
        const oscillator = audioContext.createOscillator();
        const gainNode = audioContext.createGain();
        
        if (type === 'pass') {
            oscillator.frequency.setValueAtTime(1000, audioContext.currentTime);
            gainNode.gain.setValueAtTime(0.1, audioContext.currentTime);
            oscillator.type = 'sine';
            this.soundplayed = true;
        } else {
            oscillator.type = 'sawtooth';
            oscillator.frequency.setValueAtTime(400, audioContext.currentTime);
            gainNode.gain.setValueAtTime(0.2, audioContext.currentTime);
            oscillator.frequency.linearRampToValueAtTime(50, audioContext.currentTime + 0.2);
            this.soundplayed = true;
        }
        
        oscillator.connect(gainNode);
        gainNode.connect(audioContext.destination);
        oscillator.start();
        oscillator.stop(audioContext.currentTime + 0.5);
    }
    exportToCSV() {
        if (!this.weightData.length) {
            alert('No measurements to export');
            return;
        }

        try {
            const result = DataExport.exportToCSV(this.weightData);
            DataExport.downloadFile(result.content, result.filename, result.type);
            console.log('CSV export completed');
        } catch (error) {
            console.error('CSV export error:', error);
            alert('Export failed. See console for details.');
        }
    }

    exportToJSON() {
        if (!this.weightData.length) {
            alert('No measurements to export');
            return;
        }

        try {
            const result = DataExport.exportToJSON(this.weightData);
            DataExport.downloadFile(result.content, result.filename, result.type);
            console.log('JSON export completed');
        } catch (error) {
            console.error('JSON export error:', error);
            alert('Export failed. See console for details.');
        }
    }
    rate_grams_per_second(currentWeight, currentTime) {// Calculate rate (g/s)
        if (this.lastWeightTime === 0 || this.lastWeight === 0) {
            return 0;
        }
        const timeDiff = (currentTime - this.lastWeightTime) / 1000; // Convert to seconds
        const weightDiff = currentWeight - this.lastWeight;
        console.log("rate_g/s",(weightDiff / timeDiff));
        return (weightDiff / timeDiff);
        
    }
    processWeight(weight) {
    this.uiController.updateWeightDisplay(weight);

    if (this.timerManager && this.timerManager.shouldTakeMeasurement()) {
        const currentTime = Date.now();
        const weight_tole = Math.abs(weight - this.lastWeight);
        console.log("weight_tole", weight_tole);

        if (
            weight_tole >= this.stabilityThreshold &&
            weight !== this.lastWeight &&
            Math.abs(weight) > this.stabilityThreshold
        ) {
            this.readingCount++;
            const rate = this.rate_grams_per_second(weight, currentTime);
            this.uiController.updateRateDisplay(rate);

            const reading = {
                readings: this.readingCount,
                timestamp: new Date().toLocaleString('en-UK', {
                    year: 'numeric',
                    month: '2-digit',
                    day: '2-digit',
                    hour: '2-digit',
                    minute: '2-digit',
                    second: '2-digit'
                }),
                weight: weight.toFixed(1),
                elapsedTime: this.timerManager.elapsedTime,
                rate: rate.toFixed(1)
            };

            this.lastWeight = weight;
            this.lastWeightTime = currentTime;

            this.weightData.push(reading);
            this.weightReadings.push(
                `${this.readingCount}, ${reading.weight}g, ${rate.toFixed(1)}g/s, ${reading.timestamp}`
            );

            this.uiController.displayWeightReadings(this.weightReadings);
            this.uiController.updateExportButtonStates(true);

            console.log("Measurement taken:", reading);
        }
    }
}
}
