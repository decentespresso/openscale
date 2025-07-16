class DecentScale {
    constructor() {
        // Move button initialization to DOMContentLoaded
        document.addEventListener('DOMContentLoaded', () => {
            this.initializeElements();
            this.addEventListeners();
        });

        // Keep other constructor initializations
        this.device = null;
        this.server = null;
        this.readingCount = 0;
        this.readCharacteristic = null;
        this.writeCharacteristic = null;
        this.timeout = 20;
        this.fixDroppedCommand = true;
        this.CHAR_READ = '0000fff0-0000-1000-8000-00805f9b34fb';
        this.CHAR_WRITE = '000036f5-0000-1000-8000-00805f9b34fb';
        this.ledOn = false;
        this.weightReadings = [];
        this.weightData = [];
        this.qcMode = false;
        this.qcSettings = {
            lowThreshold: 0,
            goalWeight: 0,
            highThreshold: 0,
            minWeight: 0,
            enableSounds: false
        };
        this.stabilityTimer = null;
        this.lastStableWeight = 0;
        this.commandQueue = Promise.resolve();
        this.lastWeight = 0;
        this.weightIsStable = false;
        this.stabilityThreshold = 0.2; // 0.2g threshold for stability
        this.nearzerotolerance =0.2; 
        this.countdownInterval = null;
        this.loadPresets();
        this.setupPresetHandlers();
        
        // QC State machine
        this.QC_STATES = {
            
            MEASURING: 'measuring',
            REMOVAL_PENDING: 'removal_pending',
            WAITING_FOR_NEXT: 'waiting_for_next'
        };
        this.currentQCState = this.QC_STATES.WAITING_FOR_NEXT;
        this.removalTimeout = null;
        this.removalTimeoutDuration = 5000; // 5 seconds
        this.exportCSVButton = null;
        this.exportJSONButton = null;
        //websocket 
        this.ws = new ReconnectingWebSocket(`ws://${window.location.host}/snapshot`);
        this.ws.debug =true;
    this.ws.addEventListener('message', (event) => {
        const parsedWeight = parseFloat(event.data);
        const weight = isNaN(parsedWeight) ? 0 : parsedWeight;
        this.handleWebSocketWeight(weight);
    });

    this.ws.addEventListener('open', () => {
        if (this.statusDisplay) this.statusDisplay.textContent = 'Status: Connected (WebSocket)';
    });

    this.ws.addEventListener('error', () => {
        if (this.statusDisplay) this.statusDisplay.textContent = 'Status: Connection error (WebSocket)';
    });

    this.ws.addEventListener('close', () => {
        if (this.statusDisplay) this.statusDisplay.textContent = 'Status: Disconnected (WebSocket)';
    });
    
    }

    initializeElements() {
        // Initialize export buttons first
        this.exportCSVButton = document.getElementById('exportCSV');
        this.exportJSONButton = document.getElementById('exportJSON');
        
        // Log button initialization status
        console.log('Export CSV button found:', !!this.exportCSVButton);
        console.log('Export JSON button found:', !!this.exportJSONButton);

        // Initialize other elements
        this.connectButton = document.getElementById('connectButton');
        this.weightDisplay = document.getElementById('weight');
        this.statusDisplay = document.getElementById('status');
        this.tareButton = document.getElementById('tareButton');
        this.weightReadingsList = document.getElementById('weightReadings');
        this.toggleQC = document.getElementById('toggleQC');
        //fullscreenButton
        this.fullscreenButton = document.getElementById('fullscreen-button');
        this.setupFullscreenHandler();
        
        if (!this.exportCSVButton) {
            console.warn('CSV export button not found during initialization');
        }
        if (!this.exportJSONButton) {
            console.warn('JSON export button not found during initialization');
        }
    }

    addEventListeners() {
        console.log('addEventListeners() is running');
        this.connectButton.addEventListener('click', () => this.toggleConnectDisconnect());
        this.tareButton.addEventListener('click', () => this.tare());
        // this.toggleLedButton.addEventListener('click', () => this.toggleLed());
        // this.disconnectButton.addEventListener('click', () => this.disconnect());
        // Add QC mode control listeners
        document.getElementById('toggleQC').addEventListener('click', () => this.toggleQCmode());
        // document.getElementById('stopQC').addEventListener('click', () => this.stopQCMode());
        document.getElementById('testPassSound').addEventListener('click', () => {
            this.qcSettings.enableSounds = true; // Temporarily enable sounds
            this.playSound('pass');
            setTimeout(() => {this.playSound('fail');}, 500);
        }); 
        // Add export button listeners with logging
        if (this.exportCSVButton) {
            console.log('Adding CSV export button listener');
            this.exportCSVButton.addEventListener('click', () => {
                console.log('CSV export button clicked - from listener');
                this.exportToCSV();
            });
        } else {
            console.error('CSV export button not initialized properly');
        }
        
        if (this.exportJSONButton) {
            console.log('Adding JSON export button listener');
            this.exportJSONButton.addEventListener('click', () => {
                console.log('JSON export button clicked - from listener');
                this.exportToJSON();
            });
        } else {
            console.error('JSON export button not initialized properly');
        }
    }
    //websocket

    async _findAddress() {
        try {
            const device = await navigator.bluetooth.requestDevice({
                filters: [
                    { name: 'Decent Scale' }
                ],
                optionalServices: [this.CHAR_READ]
            });
            return device;
        } catch (error) {
            console.error('Find address error:', error);
            return null;
        }
    }

    async _disconnect() {
        if (this.server) {
            await this._disable_notification();
            this.server.disconnect();
            console.log('Disconnected');
        }
        this.device = null;
        this.server = null;
        this.readCharacteristic = null;
        this.writeCharacteristic = null;
        this.statusDisplay.textContent = 'Status: Not connected';
    }

    async disconnect() {
        await this._disconnect();
        this.connectButton.textContent = 'Connect to Scale'; // <---- Update button text back to "Connect to Scale"
    }

    displayWeightReadings() {
        if (!this.weightReadingsList) {
            console.error('Weight readings list element not initialized');
            return;
        }
    
        // Clear existing readings
        this.weightReadingsList.innerHTML = '';
        
        // Display readings
        if (Array.isArray(this.weightReadings) && this.weightReadings.length > 0) {
            const reversedReadings = [...this.weightReadings].reverse();
            reversedReadings.forEach(weight => {
                const li = document.createElement('li');
                li.textContent = weight;
                li.className = 'py-1 px-2 border-b border-gray-200';
                this.weightReadingsList.appendChild(li);
            });
    
            // Enable export buttons without recreating them
            if (this.exportCSVButton && this.exportJSONButton) {
                [this.exportCSVButton, this.exportJSONButton].forEach(button => {
                    button.disabled = false;
                    button.classList.remove('opacity-50', 'bg-gray-50');
                    button.classList.add('bg-purple-400', 'hover:bg-purple-500', 'text-white');
                });
            } else {
                console.error('Export buttons not found in DOM');
            }
        } else {
            // Disable export buttons if no data
            if (this.exportCSVButton && this.exportJSONButton) {
                [this.exportCSVButton, this.exportJSONButton].forEach(button => {
                    button.disabled = true;
                    button.classList.add('opacity-50', 'bg-gray-50');
                    button.classList.remove('bg-purple-400', 'hover:bg-purple-500', 'text-white');
                });
            }
        }
    }

    exportToCSV() {
        console.log('CSV export button clicked');
        if (!this.weightData || this.weightData.length === 0) {
            console.warn('No data to export - CSV');
            return;
        }
    
        try {
            console.log('Starting CSV export with data:', this.weightData);
            const headers = [
                'Reading #',
                'Timestamp',
                'Weight (g)',
                'Result',
                'Goal Weight',
                'Low Threshold',
                'High Threshold',
                'Min Weight'
            ];
    
            const rows = this.weightData.map(reading => [
                reading.id,
                reading.timestamp,
                reading.weight,
                reading.result.toUpperCase(),
                reading.qcSettings.goalWeight,
                reading.qcSettings.lowThreshold,
                reading.qcSettings.highThreshold,
                reading.qcSettings.minWeight
            ]);
    
            const csvContent = [
                headers.join(','),
                ...rows.map(row => row.join(','))
            ].join('\n');
    
            // Format the date for filename
            const date = new Date();
            const formattedDate = date.toISOString().split('T')[0]; // Gets YYYY-MM-DD
            const filename = `qc_result_${formattedDate}.csv`;
            
            this.downloadFile(csvContent, filename, 'text/csv');
        } catch (error) {
            console.error('CSV export failed:', error);
        }
    }
    
    exportToJSON() {
        console.log('JSON export button clicked');
        if (!this.weightData || this.weightData.length === 0) {
            console.warn('No data to export - JSON');
            return;
        }
    
        try {
            console.log('Starting JSON export with data:', this.weightData);
            const jsonData = JSON.stringify({
                exportDate: new Date().toISOString(),
                readings: this.weightData
            }, null, 2);
    
            // Format the date for filename
            const date = new Date();
            const formattedDate = date.toISOString().split('T')[0]; // Gets YYYY-MM-DD
            const filename = `qc_result_${formattedDate}.json`;
            
            this.downloadFile(jsonData, filename, 'application/json');
        } catch (error) {
            console.error('JSON export failed:', error);
        }
    }
    
    downloadFile(content, filename, type) {
        try {
            const blob = new Blob([content], { type });
            const url = URL.createObjectURL(blob);
            const link = document.createElement('a');
            
            link.style.display = 'none';
            link.href = url;
            link.download = filename;
            
            document.body.appendChild(link);
            link.click();
            
            // Cleanup
            requestAnimationFrame(() => {
                document.body.removeChild(link);
                URL.revokeObjectURL(url);
            });
            
            console.log('File download initiated:', filename);
        } catch (error) {
            console.error('Download failed:', error);
            alert('Failed to download file');
        }
    }

    notification_handler(event) {
        try {
            const data = event.target.value;
            if (data.getUint8(0) !== 0x03 || data.byteLength !== 7) return;

            const type = data.getUint8(1);
            if (type === 0xCE || type === 0xCA) {
                const weight = data.getInt16(2, false) / 10;
                this.weightDisplay.textContent = `Weight: ${weight.toFixed(1)} g`;

                if (this.qcMode) {
                    // Handle QC mode, change state based on weight. 
                    console.log('Current state:', this.currentQCState, 'Current weight:', weight);
                    console.log( 'scale weight:',weight, 'userweight',this.qcSettings.minWeight);
                    

                    switch (this.currentQCState ) {
                        case this.QC_STATES.WAITING_FOR_NEXT:
                            if (weight >= this.qcSettings.minWeight) {
                                console.log('State : Waiting for next. New object detected, transitioning to measuring');
                                this.updateQCStatus(
                                    this.QC_STATES.MEASURING,
                                    'Stabilizing...'
                                );
                                console.log('State :WAITING_FOR_NEXT transitioning to measuring');
                                this.currentQCState = this.QC_STATES.MEASURING;// Update currentQCState and move to measuring state
                                this.weightIsStable = false; // Reset weightIsStable
                                this.lastWeight = weight; // Update lastWeight
                            }
                            break;

                        case this.QC_STATES.MEASURING:
                            const weightChange = Math.abs(weight - this.lastWeight);
                            console.log('Measuring State Now ,Weight change:', weightChange);
                            console.log("Weight change, threshold:", weightChange, this.stabilityThreshold);
                            console.log("Weight and minWeight (MEASURING):", weight, this.qcSettings.minWeight);
                            console.log("weightIsStable:", this.weightIsStable);
                            

                            if (weightChange <= this.stabilityThreshold ) {
                                // if (!this.weightIsStable) 
                                if (weight >= this.qcSettings.minWeight) {
                                    this.weightIsStable = true;
                                    console.log('Weight stabilized + Larger than minweight at:', weight);
                                    const result = this.evaluateWeight(weight);
                                    this.saveMeasurement(weight, result);
                                    console.log('Weight Stabled and result taken=',result);
                                    this.updateQCStatus(
                                        this.QC_STATES.REMOVAL_PENDING,
                                        'Place next object on scale'
                                    );
                                    // successfully weight record move on to next state
                                    this.currentQCState = this.QC_STATES.REMOVAL_PENDING;// Update currentQCState and move to measuring state
                                    // Set a timeout to automatically transition to WAITING_FOR_NEXT
                                    this.removalTimeout = setTimeout(() => {
                                        if (this.currentQCState === this.QC_STATES.REMOVAL_PENDING && weight >= this.qcSettings.minWeight) {
                                            console.log('W>Min,Timeout: Object not removed, transitioning to waiting state');
                                            this.weightIsStable = false;
                                            this.updateQCStatus(
                                                this.QC_STATES.REMOVAL_PENDING,
                                                'Ready - Place next object on scale (Check if previous object was left)'
                                            );
                                            console.log('Timeout+measuring done > removal pending state');

                                        }
                                    }, this.removalTimeoutDuration);}
                                    else if(weight< this.qcSettings.minWeight){
                                        this.weightIsStable = true;
                                        console.log('Weight < minweight,Weight stabilized at:', weight);
                                        const result = this.evaluateWeight(weight);
                                        this.saveMeasurement(weight, result);
                                        console.log('Weight Stabled and Result taken:',result);
                                        this.updateQCStatus(
                                            this.QC_STATES.REMOVAL_PENDING,
                                            'Place next object on scale'
                                        );
                                        console.log('user msg:(TO2)Place next object on scale');    
                                        this.currentQCState = this.QC_STATES.REMOVAL_PENDING;
                                        this.removalTimeout = setTimeout(() => {
                                            if (this.currentQCState === this.QC_STATES.REMOVAL_PENDING && weight < this.qcSettings.minWeight) {
                                                console.log('Timeout: Object not removed, transitioning from measuring to removal pending');
                                                this.weightIsStable = false;
                                                this.updateQCStatus(
                                                    this.QC_STATES.REMOVAL_PENDING,
                                                    'Ready - Place next object on scale (Check if previous object was left)'
                                                );
                                                console.log('user msg:(TO2)Ready - Place next object on scale (Check if previous object was left) ');
                                            }
                                        }, this.removalTimeoutDuration);
                                        console.log('Timeout+measuring done > removal pending state');
                                    }
                                    else if ((weight <= this.nearzerotolerance ) ){ // New condition: weight is <= 0.2 
                                        console.log("Weight is 0 . Transitioning to WAITING_FOR_NEXT.");
                                        this.weightIsStable = false; // Reset the flag
                                        this.updateQCStatus(this.QC_STATES.WAITING_FOR_NEXT, 'No Object Detected - Place next one on scale'); // Or a more appropriate message
                                        this.currentQCState = this.QC_STATES.WAITING_FOR_NEXT; // Transition to WAITING_FOR_NEXT
                                        clearTimeout(this.removalTimeout); // Clear any existing timeout.
                                    }
                            } else {
                                this.weightIsStable = false;
                                console.log('in measruing state but weight not stable');
                                // Keep showing "Stabilizing..." message
                                this.updateQCStatus(
                                    this.QC_STATES.MEASURING,
                                    'Stabilizing...'
                                );
                            }
                            break;

                        case this.QC_STATES.REMOVAL_PENDING:
                            if (weight >this.nearzerotolerance) {
                                console.log('Object not removed, ask user to remove');
                                this.weightIsStable = false;
                                // this.removalTimeout = setTimeout(() => {
                                    console.log('Timeout: Object not removed, stay at removal pending state');
                                    this.updateQCStatus(
                                        this.QC_STATES.REMOVAL_PENDING,
                                        'Remove this object, and then place the next object'
                                    );
                                    this.currentQCState = this.QC_STATES.REMOVAL_PENDING;
                                    console.log('State:removal_pending,TO3,User msg REMOVE OBJECT NOW and Place next object on scale');
                                // }, this.removalTimeoutDuration); // Closing parenthesis added
                            }
                            else if (weight == 0 || weight <= this.nearzerotolerance){
                                console.log('No weight detected assume user putting next object');
                                this.updateQCStatus(
                                    this.QC_STATES.WAITING_FOR_NEXT,
                                    'Ready ! Place next object on scale'
                                );
                                console.log('User notified, waiting object');
                                this.currentQCState = this.QC_STATES.WAITING_FOR_NEXT;
                                console.log('State: REMOVAL_PENDING, transitioned to WAITING_FOR_NEXT');
                            }
                            break;
                    }

                    this.lastWeight = weight;
                }
            }
        } catch (error) {
            console.error('Weight update error:', error);
        }
    }
    
    toggleConnectDisconnect() {
        if (this.device) { // If device is currently connected (this.device is not null)
            this.disconnect(); // Call disconnect function
        } else {
            this.connect();    // Call connect function
        }
    }
    async connect() {
        try {
            this.statusDisplay.textContent = 'Status: Scanning...';

            this.device = await this._findAddress();
            if (!this.device) {
                throw new Error('Decent Scale not found');
            }

            this.statusDisplay.textContent = 'Status: Connecting...';
            await this._connect(this.device);

            this.statusDisplay.textContent = 'Status: Connected';
            this.connectButton.textContent = 'Disconnect'; // <---- Update button text to "Disconnect"

        } catch (error) {
            console.error('Detailed error:', error);
            this.statusDisplay.textContent = `Status: Error - ${error.message}`;
            this._disconnect();
        }
    }

    async _connect(device) {
        try {
            this.server = await device.gatt.connect();
            console.log('Service found');
            const service = await this.server.getPrimaryService(this.CHAR_READ);
            console.log('Service found');

            this.readCharacteristic = await service.getCharacteristic('0000fff4-0000-1000-8000-00805f9b34fb');
            this.writeCharacteristic = await service.getCharacteristic('000036f5-0000-1000-8000-00805f9b34fb');
            console.log('Characteristics found');

            await this._enableNotification();
        } catch (error) {
            console.error('Connect error:', error);
            throw error;
        }
    }

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

    async _tare() {
        await this._send([0x03, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x0C]);
    }

    async tare() {
       
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send('tare');
            console.log('Tare requested via WebSocket');
            return; // If you only want WebSocket tare, you can return here
        } else {
            alert('WebSocket not connected.');
        }
        if (this.writeCharacteristic) {
        try {
            console.log('Tare requested via BLE');
            await this.executeCommand(() => this._tare());
        } catch (error) {
            console.error('Tare error:', error);
            if (this.statusDisplay) {
                this.statusDisplay.textContent = `Status: Tare Error - ${error.message}`;
            }
        }
        return;
    }
     // If neither is connected, show error
    alert('No scale connection for tare.');
    }

    

    async _led_on() {
        await this._send([0x03, 0x0A, 0x01, 0x01, 0x00, 0x00, 0x09]);
        this.ledOn = true;
    }

    async _led_off() {
        await this._send([0x03, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x09]);
        this.ledOn = false;
    }

    async toggleLed() {
        try {
            console.log('Toggle LED requested, current state:', this.ledOn);
            if (this.ledOn) {
                await this.executeCommand(() => this._led_off());
            } else {
                await this.executeCommand(() => this._led_on());
            }
        } catch (error) {
            console.error('Toggle LED error:', error);
            this.statusDisplay.textContent = `Status: LED Toggle Error - ${error.message}`;
        }
    }

    async _enableNotification() {
        try {
            await this.readCharacteristic.startNotifications();
            this.readCharacteristic.addEventListener('characteristicvaluechanged',
                (event) => this.notification_handler(event));
        } catch (error) {
            console.error('Enable notification error:', error);
            throw error;
        }
    }
    async _disable_notification() {
        try {
            await this.readCharacteristic.stopNotifications();
            this.readCharacteristic.removeEventListener('characteristicvaluechanged',
                (event) => this.notification_handler(event));
        } catch (error) {
            console.error('Disable notification error:', error);
            throw error;
        }
    }
    evaluateWeight(weight) {
        if (weight >= this.qcSettings.lowThreshold && 
            weight <= this.qcSettings.highThreshold) {
            return 'pass';
        }
        return 'fail';
    }

    playSound(type) {
        console.log('Attempting to play sound:', type, 'Sounds enabled:', this.qcSettings.enableSounds);
        if (!this.qcSettings.enableSounds) {
            console.log('Sounds are disabled, not playing');
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
                console.log('Configured pass sound');
            } else {
                oscillator.type = 'sawtooth';
                oscillator.frequency.setValueAtTime(400, audioContext.currentTime);
                gainNode.gain.setValueAtTime(0.2, audioContext.currentTime);
                oscillator.frequency.linearRampToValueAtTime(50, audioContext.currentTime + 0.2);
                console.log('Configured fail sound');
            }
            
            oscillator.connect(gainNode);
        gainNode.connect(audioContext.destination);
        
        // Ensure oscillator has time to play
        oscillator.start();
        console.log('Sound started');
        
        setTimeout(() => {
            oscillator.stop();
            audioContext.close();
            console.log('Sound stopped');
        }, 300); // Increased from 200ms to 300ms
        
    } catch (error) {
        console.error('Sound playback failed:', error);
    }
}
    toggleQCmode(){
        if (!this.toggleQC) {
            console.error('Toggle QC button not initialized');
            return;
        }
        if (this.qcMode) {
            this.stopQCMode();
            this.qcMode = false;
            this.toggleQC.textContent = 'Start QC Mode';
            console.log("stopQC, button text changed");
        } else {
            this.startQCMode();
            this.qcMode=true;
            this.toggleQC.textContent = 'Stop QC Mode';
            console.log("StartQC, button text changed");
        }
    }

    startQCMode() {
        console.log('Starting QC Mode');
        this.qcMode = true;
        this.qcSettings = {
            lowThreshold: parseFloat(document.getElementById('lowThreshold').value),
            goalWeight: parseFloat(document.getElementById('goalWeight').value),
            highThreshold: parseFloat(document.getElementById('highThreshold').value),
            minWeight: parseFloat(document.getElementById('minWeight').value),
            enableSounds: document.getElementById('enableSounds').checked,
            // waitTime: parseInt(document.getElementById('waitTime').value, 10) || 2
        };
        this.tare();
        // Set initial state when starting QC mode
        this.updateQCStatus(
            this.QC_STATES.WAITING_FOR_NEXT,
            'QC Mode: Active - Place object on scale'
        );
        console.log('QC Mode started, current state:', this.currentQCState);
    }

    updateCountdown(timeLeft) {
        const qcStatus = document.getElementById('qcStatus');
        if (timeLeft > 0) {
            qcStatus.textContent = `Measurement done, place next object. Next measurement starts in
             ${timeLeft}s`;
            qcStatus.className = 'qc-status waiting text-lg font-bold text-red-500 text-center animate-pulse';
        } else {
            qcStatus.textContent = 'Place next object on scale';
            qcStatus.className = 'qc-status waiting text-lg font-bold text-blue-500 text-center animate-pulse';
        }
    }

    updateMeasurementStatus(timeLeft) {
        const qcStatus = document.getElementById('qcStatus');
        qcStatus.textContent = `Measuring in progress: ${timeLeft}s`;
        qcStatus.className = 'qc-status measuring';
    }

    stopQCMode() {
        this.qcMode = false;
    if (this.countdownInterval) {
        clearInterval(this.countdownInterval);
        this.countdownInterval = null;
    }
    this.stabilityTimer = null;
    this.weightIsStable = false;
    this.updateQCStatus(
        this.QC_STATES.WAITING_FOR_NEXT,
        'QC Mode: Inactive'
    );
    console.log('QC Mode stopped');
    }

    setupPresetHandlers() {
        // MODIFIED event listener for presetSelect dropdown to handle both load and save
        document.getElementById('presetSelect').addEventListener('change', (e) => {
            const selectedValue = e.target.value;
            if (selectedValue === 'save_preset') {
                this.saveCurrentPreset(); // Call save functionality when "save_preset" is selected
                e.target.value = ''; // Reset dropdown after saving (optional, for better UX)
            } else if (selectedValue) {
                this.loadPreset(selectedValue); // Existing load preset functionality
            }
        });
    
        // Keep the event listener for objectName input - test if it still works as expected
        document.getElementById('objectName').addEventListener('input', (e) => {
            const presetSelect = document.getElementById('presetSelect');
            if (this.getPreset(e.target.value)) {
                presetSelect.value = e.target.value;
            }
        });
    }

    saveCurrentPreset() {
        const objectName = document.getElementById('objectName').value.trim();
        if (!objectName) {
            alert('Please enter an object name');
            return;
        }

        const preset = {
            name: objectName,
            settings: {
                lowThreshold: parseFloat(document.getElementById('lowThreshold').value),
                goalWeight: parseFloat(document.getElementById('goalWeight').value),
                highThreshold: parseFloat(document.getElementById('highThreshold').value),
                minWeight: parseFloat(document.getElementById('minWeight').value),
                // waitTime: parseInt(document.getElementById('waitTime').value, 10) || 2
            }
        };

        this.savePreset(preset);
        this.updatePresetList();
        alert(`Preset "${objectName}" saved successfully`);
    }

    savePreset(preset) {
        const presets = this.getPresets();
        presets[preset.name] = preset.settings;
        localStorage.setItem('decentScalePresets', JSON.stringify(presets));
    }

    getPresets() {
        const presetsJson = localStorage.getItem('decentScalePresets');
        return presetsJson ? JSON.parse(presetsJson) : {};
    }

    getPreset(name) {
        const presets = this.getPresets();
        return presets[name];
    }

    loadPresets() {
        const presets = this.getPresets();
        const presetSelect = document.getElementById('presetSelect');

        // **Preserve "Save Preset" option:**
        const savePresetOption = presetSelect.querySelector('option[value="save_preset"]'); // Get the "Save Preset" option

        // Clear *only* the preset options (leave "Select a preset..." and "Save Preset")
        while (presetSelect.options.length > 1) { // Keep condition > 1, but now we are careful about what's at index 1
            if (presetSelect.options[1] !== savePresetOption) { // Check if option at index 1 is NOT the "Save Preset" option
                presetSelect.remove(1); // If it's not the "Save Preset" option, remove it (this will remove dynamically loaded presets)
            } else {
                // If it IS the "Save Preset" option at index 1 (which should not happen in correct HTML, but for safety), 
                // break to avoid infinite loop
                break;
            }
        }


        // Add presets to dropdown
        Object.keys(presets).forEach(name => {
            const option = document.createElement('option');
            option.value = name;
            option.textContent = name;
            presetSelect.appendChild(option);
        });

        // Load last used preset if available (keep this part)
        const lastUsed = localStorage.getItem('lastUsedPreset');
        if (lastUsed && presets[lastUsed]) {
            this.loadPreset(lastUsed);
            presetSelect.value = lastUsed;
        }
    }

    loadPreset(name) {
        const preset = this.getPreset(name);
        if (!preset) return;

        document.getElementById('objectName').value = name;
        document.getElementById('lowThreshold').value = preset.lowThreshold;
        document.getElementById('goalWeight').value = preset.goalWeight;
        document.getElementById('highThreshold').value = preset.highThreshold;
        document.getElementById('minWeight').value = preset.minWeight;
        // document.getElementById('waitTime').value = preset.waitTime || 2;

        localStorage.setItem('lastUsedPreset', name);
    }

    updatePresetList() {
        this.loadPresets();
    }

    updateQCStatus(state, message) {
        const qcStatus = document.getElementById('qcStatus');
        qcStatus.textContent = message;

        // State-specific styling
        switch(state) {
            case this.QC_STATES.MEASURING:
                qcStatus.className = 'mt-4 p-4 rounded-lg shadow-md text-center text-lg font-bold bg-blue-100 text-blue-700';
                break;
            case this.QC_STATES.REMOVAL_PENDING:
                qcStatus.className = 'mt-4 p-4 rounded-lg shadow-md text-center text-lg font-bold bg-red-100 text-red-700 animate-pulse';
                break;
            case this.QC_STATES.WAITING_FOR_NEXT:
                qcStatus.className = 'mt-4 p-4 rounded-lg shadow-md text-center text-lg font-bold bg-green-100 text-green-700';
                break;
            default:
                qcStatus.className = 'mt-4 p-4 rounded-lg shadow-md text-center text-lg font-bold bg-gray-100';
        }
    }

    saveMeasurement(weight, result) {
        if (!Array.isArray(this.weightData)) {
            this.weightData = [];
        }
        
        // Increment readingCount before using it
        this.readingCount = this.weightData.length + 1;
        
        const reading = {
            id: this.readingCount,  // Now uses the correct sequence number
            timestamp: new Date().toISOString(),
            weight: parseFloat(weight.toFixed(1)),
            result: result,
            qcSettings: { ...this.qcSettings }
        };
    
        this.weightData.push(reading);
        // Format the display string with the correct sequence number
        this.weightReadings.push(`${reading.id}. ${new Date(reading.timestamp).toLocaleString()}: ${reading.weight}g - ${result.toUpperCase()}`);
        
        console.log('Measurement saved:', reading);
        this.displayWeightReadings();
        
        if (this.qcSettings.enableSounds) {
            this.playSound(result);
        }
    }
     //make body go full screen 
     toggleFullScreen() {
        if (!document.fullscreenElement) {
            document.body.requestFullscreen();
            document.body.setAttribute("fullscreen", "");
        } else {
            document.exitFullscreen();
            document.body.removeAttribute("fullscreen");
        }
    }

    setupFullscreenHandler() {
        if (document.fullscreenEnabled && this.fullscreenButton) {
            this.fullscreenButton.addEventListener('click', () => this.toggleFullScreen());
            document.addEventListener('fullscreenchange', () => this.updateFullscreenState());
        } else if (this.fullscreenButton) {
            this.fullscreenButton.style.display = 'none';
        }
    }

    updateFullscreenState() {
        if (document.fullscreenElement) {
            document.body.setAttribute("fullscreen", "");
        } else {
            document.body.removeAttribute("fullscreen");
        }
    }
    //websocket methods 
    handleWebSocketWeight(weight) {
    // Update the weight display
    if (this.weightDisplay) {
        this.weightDisplay.textContent = `Weight: ${weight.toFixed(1)} g`;
    }
    // If QC mode is active, run QC logic
    if (this.qcMode) {
        this.processQCWeight(weight);
    }
}
    processQCWeight(weight) {
        try {
        // Directly use the weight value from WebSocket
        this.weightDisplay.textContent = `Weight: ${weight.toFixed(1)} g`;

        if (this.qcMode) {
            // QC state machine logic
            switch (this.currentQCState) {
                case this.QC_STATES.WAITING_FOR_NEXT:
                    if (weight >= this.qcSettings.minWeight) {
                        this.updateQCStatus(
                            this.QC_STATES.MEASURING,
                            'Stabilizing...'
                        );
                        this.currentQCState = this.QC_STATES.MEASURING;
                        this.weightIsStable = false;
                        this.lastWeight = weight;
                    }
                    break;

                case this.QC_STATES.MEASURING:
                    const weightChange = Math.abs(weight - this.lastWeight);

                    if (weightChange <= this.stabilityThreshold) {
                        if (weight >= this.qcSettings.minWeight) {
                            this.weightIsStable = true;
                            const result = this.evaluateWeight(weight);
                            this.saveMeasurement(weight, result);
                            this.updateQCStatus(
                                this.QC_STATES.REMOVAL_PENDING,
                                'Place next object on scale'
                            );
                            this.currentQCState = this.QC_STATES.REMOVAL_PENDING;
                            this.removalTimeout = setTimeout(() => {
                                if (this.currentQCState === this.QC_STATES.REMOVAL_PENDING && weight >= this.qcSettings.minWeight) {
                                    this.weightIsStable = false;
                                    this.updateQCStatus(
                                        this.QC_STATES.REMOVAL_PENDING,
                                        'Ready - Place next object on scale (Check if previous object was left)'
                                    );
                                }
                            }, this.removalTimeoutDuration);
                        } else if (weight < this.qcSettings.minWeight) {
                            this.weightIsStable = true;
                            const result = this.evaluateWeight(weight);
                            this.saveMeasurement(weight, result);
                            this.updateQCStatus(
                                this.QC_STATES.REMOVAL_PENDING,
                                'Place next object on scale'
                            );
                            this.currentQCState = this.QC_STATES.REMOVAL_PENDING;
                            this.removalTimeout = setTimeout(() => {
                                if (this.currentQCState === this.QC_STATES.REMOVAL_PENDING && weight < this.qcSettings.minWeight) {
                                    this.weightIsStable = false;
                                    this.updateQCStatus(
                                        this.QC_STATES.REMOVAL_PENDING,
                                        'Ready - Place next object on scale (Check if previous object was left)'
                                    );
                                }
                            }, this.removalTimeoutDuration);
                        } else if (weight <= this.nearzerotolerance) {
                            this.weightIsStable = false;
                            this.updateQCStatus(
                                this.QC_STATES.WAITING_FOR_NEXT,
                                'No Object Detected - Place next one on scale'
                            );
                            this.currentQCState = this.QC_STATES.WAITING_FOR_NEXT;
                            clearTimeout(this.removalTimeout);
                        }
                    } else {
                        this.weightIsStable = false;
                        this.updateQCStatus(
                            this.QC_STATES.MEASURING,
                            'Stabilizing...'
                        );
                    }
                    break;

                case this.QC_STATES.REMOVAL_PENDING:
                    if (weight > this.nearzerotolerance) {
                        this.weightIsStable = false;
                        this.updateQCStatus(
                            this.QC_STATES.REMOVAL_PENDING,
                            'Remove this object, and then place the next object'
                        );
                        this.currentQCState = this.QC_STATES.REMOVAL_PENDING;
                    } else if (weight === 0 || weight <= this.nearzerotolerance) {
                        this.updateQCStatus(
                            this.QC_STATES.WAITING_FOR_NEXT,
                            'Ready ! Place next object on scale'
                        );
                        this.currentQCState = this.QC_STATES.WAITING_FOR_NEXT;
                    }
                    break;
            }

            this.lastWeight = weight;
        }
    } catch (error) {
        console.error('Weight update error:', error);
    }
}
}