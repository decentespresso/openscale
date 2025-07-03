class DecentScale {
    constructor() {
        // Move button initialization to DOMContentLoaded
        document.addEventListener('DOMContentLoaded', () => {
            this.initializeElements();
            this.addEventListeners();
        });

        // Keep other constructor initializations
        this.socket = null; // WebSocket connection
        this.readingCount = 0;
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

    toggleConnectDisconnect() {
        if (this.socket && (this.socket.readyState === WebSocket.OPEN || this.socket.readyState === WebSocket.CONNECTING)) {
            this.disconnect(); // Call disconnect function
        } else {
            this.connect();    // Call connect function
        }
    }

    connect() {
        try {
            this.statusDisplay.textContent = 'Status: Connecting to bridge...';
            this.socket = new WebSocket('ws://localhost:8080');

            this.socket.onopen = () => {
                console.log('WebSocket connection established.');
                this.statusDisplay.textContent = 'Status: Searching for scale...';
                this.connectButton.textContent = 'Disconnect';
            };

            this.socket.onmessage = (event) => {
                this.handleServerMessage(event);
            };

            this.socket.onclose = () => {
                console.log('WebSocket connection closed.');
                this.statusDisplay.textContent = 'Status: Not connected';
                this.connectButton.textContent = 'Connect to Scale';
                this.socket = null;
            };

            this.socket.onerror = (error) => {
                console.error('WebSocket Error:', error);
                this.statusDisplay.textContent = 'Status: Bridge connection error';
            };
        } catch (error) {
            console.error('Detailed error:', error);
            this.statusDisplay.textContent = `Status: Error - ${error.message}`;
        }
    }

    disconnect() {
        if (this.socket) {
            this.socket.close();
        }
    }

    tare() {
        if (this.socket && this.socket.readyState === WebSocket.OPEN) {
            console.log('Sending tare command via WebSocket');
            this.socket.send(JSON.stringify({ command: 'tare' }));
        } else {
            console.error('Cannot tare: WebSocket is not connected.');
        }
    }

    evaluateWeight(weight) {
        if (weight >= this.qcSettings.lowThreshold && 
            weight <= this.qcSettings.highThreshold) {
            return 'pass';
        }
        return 'fail';
    }

    handleServerMessage(event) {
        try {
            const message = JSON.parse(event.data);

            if (message.type === 'status') {
                this.statusDisplay.textContent = `Status: ${message.message}`;
                if (message.message === 'Connected') {
                    this.connectButton.textContent = 'Disconnect';
                }
            } else if (message.type === 'weight') {
                const weight = message.value;
                this.weightDisplay.textContent = `Weight: ${weight.toFixed(1)} g`;

                if (this.qcMode) {
                    this.processQC(weight);
                }
            }
        } catch (error) {
            console.error('Error handling server message:', error);
        }
    }

    processQC(weight) {
        // This is the logic from your original notification_handler, now in its own method.
        console.log('Current state:', this.currentQCState, 'Current weight:', weight);
        console.log('scale weight:', weight, 'userweight', this.qcSettings.minWeight);

        switch (this.currentQCState) {
            case this.QC_STATES.WAITING_FOR_NEXT:
                if (weight >= this.qcSettings.minWeight) {
                    console.log('State : Waiting for next. New object detected, transitioning to measuring');
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
                        console.log('Weight stabilized + Larger than minweight at:', weight);
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
                        console.log('Weight < minweight,Weight stabilized at:', weight);
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
                    } else if ((weight <= this.nearzerotolerance)) {
                        this.weightIsStable = false;
                        this.updateQCStatus(this.QC_STATES.WAITING_FOR_NEXT, 'No Object Detected - Place next one on scale');
                        this.currentQCState = this.QC_STATES.WAITING_FOR_NEXT;
                        clearTimeout(this.removalTimeout);
                    }
                } else {
                    this.weightIsStable = false;
                    this.updateQCStatus(this.QC_STATES.MEASURING, 'Stabilizing...');
                }
                break;

            case this.QC_STATES.REMOVAL_PENDING:
                if (weight > this.nearzerotolerance) {
                    this.weightIsStable = false;
                    this.updateQCStatus(this.QC_STATES.REMOVAL_PENDING, 'Remove this object, and then place the next object');
                    this.currentQCState = this.QC_STATES.REMOVAL_PENDING;
                } else if (weight <= this.nearzerotolerance) {
                    this.updateQCStatus(this.QC_STATES.WAITING_FOR_NEXT, 'Ready ! Place next object on scale');
                    this.currentQCState = this.QC_STATES.WAITING_FOR_NEXT;
                }
                break;
        }
        this.lastWeight = weight;
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
}
