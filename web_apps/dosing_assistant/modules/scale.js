import { SCALE_CONSTANTS } from './constants.js';
import { StateMachine } from './state-machine.js';
import { UIController } from './ui-controller.js';
import { DataExport } from './export.js';

export class DecentScale {
    constructor(uiController, stateMachine) {
        // Basic scale properties
        this.device = null;
        this.server = null;
        this.readCharacteristic = null;
        this.writeCharacteristic = null;
        this.timeout = 20;
        this.fixDroppedCommand = true;
        this.CHAR_READ = SCALE_CONSTANTS.CHAR_READ;
        this.CHAR_WRITE = SCALE_CONSTANTS.CHAR_WRITE;
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
        this.stabilityThreshold = 0.4;
        this.stabilityDuration = 4;
        this.doseCompletedAndSaved = false;
        this.containerReplacedWeightReadings = [];
        this.containerReplacementStabilityCheckTimeout = null;
        this.autoRestartDoseSaved = false;
        
        // State machine
        this.stateMachine = stateMachine;
        this.uiController = uiController;
        //this.stateMachine = new StateMachine(uiController);
        // Reading counter
        this.readingCount = 0;
        this.soundplayed = false; 
        this.soundEnabled = true; // Default to enabled
        console.log('Sound initialized:', { enabled: this.soundEnabled });
        this.initializeSoundPreference();
    }

    initializeSoundPreference() {
        const soundToggle = document.getElementById('soundEnabled');
        if (soundToggle) {
            // Load saved preference
            const savedPreference = localStorage.getItem('soundEnabled');
            if (savedPreference !== null) {
                this.soundEnabled = savedPreference === 'true';
                soundToggle.checked = this.soundEnabled;
                console.log('Loaded sound preference:', { enabled: this.soundEnabled });
            }

            // Add listener
            soundToggle.addEventListener('change', (e) => {
                this.updateSoundPreference(e.target.checked);
                localStorage.setItem('soundEnabled', e.target.checked);
                console.log('Sound preference changed:', { enabled: e.target.checked });
            });
        } else {
            console.warn('Sound toggle element not found');
        }
    }

    // Bluetooth connection methods
    async connect() {
        try {
            this.uiController.updateStatus('Scanning...');
            
            this.device = await this._findAddress();
            if (!this.device) {
                throw new Error('Decent Scale not found');
            }

            this.uiController.updateStatus('Connecting...');
            this.uiController.updateConnectButton('Connecting');
            await this._connect(this.device);

            this.uiController.updateStatus('Connected');
            this.uiController.updateConnectButton('Disconnect');
            this.uiController.updateGuidance('Scale connected. Click Start to begin.');
            
            return true;
        } catch (error) {
            console.error('Connection error:', error);
            this.uiController.updateStatus('Connection failed');
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
        await this._disconnect();
        this.uiController.updateConnectButton('Connect to Scale');
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
        if (!this.writeCharacteristic) {
            throw new Error('Device not connected');
        }
        
        await this.writeCharacteristic.writeValue(new Uint8Array(cmd));
        if (this.fixDroppedCommand) {
            await new Promise(resolve => setTimeout(resolve, 50));
            await this.writeCharacteristic.writeValue(new Uint8Array(cmd));
        }
    }

    async tare() {
        await this._tare();
    }

    async _tare() {
        await this._send([0x03, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x0C]);
    }

    // Notification handler
    notification_handler(event) {
        console.log('--- notification_handler START ---');
        console.log('Current Flags at notification_handler start:', {
            dosingMode: this.dosingMode,
            dosingPausedForContainerRemoval: this.dosingPausedForContainerRemoval,
            doseCompletedAndSaved: this.doseCompletedAndSaved
        });

        try {
            const data = event.target.value;
            if (data.getUint8(0) !== 0x03 || data.byteLength !== 7) {
                return;
            }

            const type = data.getUint8(1);
            if (type === 0xCE || type === 0xCA) {
                const weight = data.getInt16(2, false) / 10;
                const netWeight = weight - this.tareWeight;
                
                console.log('TareWeight reading:', this.tareWeight, 'netWeight reading:', netWeight);
                
                // Update the weight display directly
                document.getElementById('weight').textContent = `Weight: ${netWeight.toFixed(1)}g`;
                
                if (this.dosingMode) {
                    if (this.waitingForContainerWeight) {
                        console.log('Dosing mode active, but waiting for container weight setup. Dosing logic skipped.');
                        document.getElementById('guidance').textContent = 'Please set container weight by clicking the button below.';
                        return;
                    }
                    else
                    console.log('Current weight reading /notification_handler start/:', {
                        raw: weight,
                        net: netWeight,
                        tare: this.tareWeight
                    });
                    
                    // Add to stable weight readings array for stability check
                    this.stableWeightReadings.push(netWeight);
                    if (this.stableWeightReadings.length > this.stabilityDuration) {
                        this.stableWeightReadings.shift();
                    }
                    
                    // Call handleDosingMode before state machine update
                    this.handleDosingMode(netWeight);
                    console.log("pass to state mahcine to handle doing.")
                            // And again here - remove this line
        // this.stateMachine.handleWeightUpdate(netWeight, this);
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
            return 'Not enough weight.';
        } else if (weight > highThreshold) {
            return 'Weight exceeded.';
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
        console.log('Attempting to play sound:', { 
            type, 
            enabled: this.soundEnabled, 
            alreadyPlayed: this.soundplayed 
        });

        if (this.soundplayed || !this.soundEnabled) {
            console.log('Sound skipped:', { 
                reason: this.soundplayed ? 'already played' : 'sound disabled' 
            });
            return;
        }
        
        try {
            const audioContext = new (window.AudioContext || window.webkitAudioContext)();
            const oscillator = audioContext.createOscillator();
            const gainNode = audioContext.createGain();
            
            if (type === 'pass') {
                oscillator.frequency.setValueAtTime(1000, audioContext.currentTime);
                gainNode.gain.setValueAtTime(0.1, audioContext.currentTime);
                oscillator.type = 'sine';
                console.log('Playing success sound');
            } else {
                oscillator.type = 'sawtooth';
                oscillator.frequency.setValueAtTime(400, audioContext.currentTime);
                gainNode.gain.setValueAtTime(0.2, audioContext.currentTime);
                oscillator.frequency.linearRampToValueAtTime(50, audioContext.currentTime + 0.2);
                console.log('Playing fail sound');
            }
            
            this.soundplayed = true;
            oscillator.connect(gainNode);
            gainNode.connect(audioContext.destination);
            oscillator.start();
            
            setTimeout(() => {
                oscillator.stop();
                audioContext.close();
                this.soundplayed = false;
                console.log('Sound completed and reset');
            }, 300);
            
        } catch (error) {
            console.error('Sound playback failed:', error);
        }
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
    updateSoundPreference(enabled) {
        this.soundEnabled = enabled;
        console.log('Sound preference updated:', { 
            enabled: this.soundEnabled,
            storedPreference: localStorage.getItem('soundEnabled')
        });
    }

}
