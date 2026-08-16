export class UIController {
    constructor(timerManager) {  // Add timerManager parameter
        // Timer elements
        this.timerManager = timerManager;  // Store timerManager reference
        this.timerDisplay = document.getElementById('timer');
        this.toggleButton = document.getElementById('toggleTimer');
        this.timerOn = false;
        this.weightDisplay = document.getElementById('weight');
        this.statusDisplay = document.getElementById('status');
        this.measurementAlert = document.getElementById('measurementAlert');
        this.weightReadingsList = document.getElementById('weightReadings');
        
        // Export buttons
        this.exportCSVButton = document.getElementById('exportCSV');
        this.exportJSONButton = document.getElementById('exportJSON');
        //ratedisplay
        this.rateDisplay = document.getElementById('rate');
        this.guidanceDisplay = document.getElementById('guidance');
        this.progressFill = document.getElementById('progressFill');
        this.progressPercent = document.getElementById('progressPercent');
        this.setContainerWeightButton = document.getElementById('setContainerWeightButton');
        this.connectButton = document.getElementById('connect');
        this.dosingToggleButton = document.getElementById('dosingToggleButton');
        this.durationInput = document.getElementById('duration');
        this.intervalInput = document.getElementById('interval');
        // Add input elements
        this.targetWeightInput = document.getElementById('targetWeight');
        this.lowThresholdInput = document.getElementById('lowThreshold');
        this.highThresholdInput = document.getElementById('highThreshold');
        
        // Initialize input listeners
        this.setupTargetWeightListener();
        this.fullscreenButton = document.getElementById('fullscreen-button');
        this.setupFullscreenHandler();
        console.log('UI Controller initialized with elements:', {
            weightDisplay: !!this.weightDisplay,
            statusDisplay: !!this.statusDisplay,
            guidanceDisplay: !!this.guidanceDisplay,
            progressFill: !!this.progressFill,
            progressPercent: !!this.progressPercent,
            weightReadingsList: !!this.weightReadingsList
        });
    }

    // Add this new method
    setupTargetWeightListener() {
        if (this.targetWeightInput) {
            this.targetWeightInput.addEventListener('input', () => {
                const targetWeightValue = parseFloat(this.targetWeightInput.value);
                
                if (!isNaN(targetWeightValue)) {
                    // Set thresholds to target ± 1.0g
                    if (this.lowThresholdInput) {
                        this.lowThresholdInput.value = (targetWeightValue - 1.0).toFixed(1);
                    }
                    if (this.highThresholdInput) {
                        this.highThresholdInput.value = (targetWeightValue + 1.0).toFixed(1);
                    }
                    console.log('Updated thresholds:', {
                        target: targetWeightValue,
                        low: this.lowThresholdInput?.value,
                        high: this.highThresholdInput?.value
                    });
                } else {
                    // Clear thresholds if target is invalid
                    if (this.lowThresholdInput) this.lowThresholdInput.value = '';
                    if (this.highThresholdInput) this.highThresholdInput.value = '';
                }
            });
            console.log('Target weight input listener set up');
        } else {
            console.warn('Target weight input element not found');
        }
    }

    // Weight display methods
    updateWeightDisplay(weight) {
        if (this.weightDisplay) {
            this.weightDisplay.textContent = `Weight: ${weight.toFixed(1)} g`;
        }
    }
    updateRateDisplay(rate) {
        if (this.rateDisplay) {
            this.rateDisplay.textContent = `Rate: ${rate.toFixed(1)} g/s`;
        }
    }

    getWeightDisplay() {
        if (this.weightDisplay) {
            const weightText = this.weightDisplay.textContent;
            return parseFloat(weightText.replace('Weight: ', '').replace('g', ''));
        }
        return 0;
    }

    // Progress bar methods
    updateProgressBar(percentage) {
        if (this.progressFill) {
            this.progressFill.style.width = `${percentage}%`;
        }
        if (this.progressPercent) {
            this.progressPercent.textContent = `${percentage.toFixed(1)}%`;
        }
    }

    updateProgressBarColor(status) {
        if (!this.progressFill) return;

        this.progressFill.classList.remove(
            'bg-gray-300', 
            'bg-yellow-500', 
            'bg-green-500', 
            'bg-red-600'
        );

        switch (status) {
            case 'success':
                this.progressFill.classList.add('bg-green-500');
                break;
            case 'warning':
                this.progressFill.classList.add('bg-yellow-500');
                break;
            case 'error':
                this.progressFill.classList.add('bg-red-600');
                break;
            default:
                this.progressFill.classList.add('bg-gray-300');
        }
    }

    // Status and guidance methods
    updateStatus(message) {
        if (this.statusDisplay) {
            this.statusDisplay.textContent = `Status: ${message}`;
        }
    }

    updateGuidance(message, type = 'info') {
        if (this.guidanceDisplay) {
            this.guidanceDisplay.textContent = message;
            
            // Update guidance styling based on type
            this.guidanceDisplay.className = 'mt-4 p-4 rounded-lg shadow-md text-center text-lg font-bold';
            switch (type) {
                case 'success':
                    this.guidanceDisplay.classList.add('bg-green-100', 'text-green-800');
                    break;
                case 'warning':
                    this.guidanceDisplay.classList.add('bg-yellow-100', 'text-yellow-800');
                    break;
                case 'error':
                    this.guidanceDisplay.classList.add('bg-red-100', 'text-red-800');
                    break;
                default:
                    this.guidanceDisplay.classList.add('bg-blue-100', 'text-blue-800');
            }
        }
    }

    // Weight readings display
    displayWeightReadings(weightReadings) {
        if (!this.weightReadingsList) return;
        
        this.weightReadingsList.innerHTML = '';
        const hasData = Array.isArray(weightReadings) && weightReadings.length > 0;
        
        if (hasData) {
            const table = document.createElement('table');
            table.className = 'w-full border-collapse';
            
            // Header row
            const thead = document.createElement('thead');
            const headerRow = document.createElement('tr');
            headerRow.innerHTML = `
                <th class="w-24 text-left py-2 px-4 text-black border-b-2 border-gray-300">Reading #</th>
                <th class="w-32 text-left py-2 px-4 text-black border-b-2 border-gray-300">Weight (g)</th>
                <th class="w-32 text-left py-2 px-4 text-black border-b-2 border-gray-300">Rate (g/s)</th>
                <th class="flex-1 text-left py-2 px-4 text-black border-b-2 border-gray-300">Date</th>
            `;
            thead.appendChild(headerRow);
            table.appendChild(thead);
    
            const tbody = document.createElement('tbody');
            const reversedReadings = [...weightReadings].reverse();
            
            reversedReadings.forEach(reading => {
                // Modify how we split and process the data
                const [readingNum, weight, rateWithParens, ...timestampParts] = reading.split(',').map(s => s.trim());
                
                // Clean up the rate string - remove parentheses and quotes
                const rate = rateWithParens ? rateWithParens.replace(/[()]/g, '').replace(/"/g, '') : '';
                
                // Combine timestamp parts back together
                const timestamp = timestampParts.join(', ').trim();
                
                const row = document.createElement('tr');
                row.innerHTML = `
                    <td class="py-1 px-4 border-b border-gray-200">${readingNum}</td>
                    <td class="py-1 px-4 border-b border-gray-200">${weight}</td>
                    <td class="py-1 px-4 border-b border-gray-200">${rate}</td>
                    <td class="py-1 px-4 border-b border-gray-200">${timestamp}</td>
                `;
                tbody.appendChild(row);
            });
            
            table.appendChild(tbody);
            this.weightReadingsList.appendChild(table);
        }
    
        this.updateExportButtonStates(hasData);
    }

    // Button control methods
    updateConnectButton(text) {
        if (this.connectButton) {
            console.log('Updating connect button text to:', text);
            this.connectButton.textContent = text;
            // Update button styling based on connection state
            if (text === 'Disconnect') {
                this.connectButton.classList.add('bg-red-400', 'text-white');
                this.connectButton.classList.remove('bg-purple-100', 'hover:bg-purple-200');
            } else {
                this.connectButton.classList.add('bg-purple-100', 'hover:bg-purple-200');
                this.connectButton.classList.remove('bg-red-400', 'text-white');
            }
        } else {
            console.error('Cannot update connect button - element not found');
        }
    }

    updateDosingButton(text, isActive) {
        if (this.dosingToggleButton) {
            this.dosingToggleButton.textContent = text;
            if (isActive) {
                this.dosingToggleButton.classList.replace('bg-purple-400', 'bg-red-600');
            } else {
                this.dosingToggleButton.classList.replace('bg-red-600', 'bg-purple-400');
            }
        }
    }

    enableSetContainerWeightButton() {
        if (this.setContainerWeightButton) {
            this.setContainerWeightButton.removeAttribute('disabled');
            this.setContainerWeightButton.classList.replace('bg-gray-400', 'bg-teal-500');
        }
    }

    disableSetContainerWeightButton() {
        if (this.setContainerWeightButton) {
            this.setContainerWeightButton.setAttribute('disabled', '');
            this.setContainerWeightButton.classList.replace('bg-teal-500', 'bg-gray-400');
        }
    }

    showSetContainerWeightButton() {
        if (this.setContainerWeightButton) {
            this.setContainerWeightButton.classList.remove('invisible');
        }
        console.log("showSetContainerWeightButton")
    }

    hideSetContainerWeightButton() {
        if (this.setContainerWeightButton) {
            this.setContainerWeightButton.classList.add('invisible');
        }
    }

    enableStartDosingButton() {
        if (this.dosingToggleButton) {
            this.dosingToggleButton.removeAttribute('disabled');
            this.dosingToggleButton.classList.remove('opacity-50', 'cursor-not-allowed');
            console.log('Start dosing button enabled');
        }
    }

    disableStartDosingButton() {
        if (this.dosingToggleButton) {
            this.dosingToggleButton.setAttribute('disabled', '');
            this.dosingToggleButton.classList.add('opacity-50', 'cursor-not-allowed');
            console.log('Start dosing button disabled');
        }
    }

    // Form input methods
    getTargetWeight() {
        const input = document.getElementById('targetWeight');
        return input ? input.value : '0';
    }

    getLowThreshold() {
        const input = document.getElementById('lowThreshold');
        return input ? input.value : '0';
    }

    getHighThreshold() {
        const input = document.getElementById('highThreshold');
        return input ? input.value : '0';
    }

    updateExportButtonStates(hasData) {
        console.log('Updating export buttons state:', hasData ? 'enabled' : 'disabled');

        const enabledClasses = ['bg-purple-100', 'text-purple-800', 'border-purple-300', 'hover:bg-purple-200'];
        const disabledClasses = ['bg-gray-50', 'opacity-50', 'cursor-not-allowed'];

        if (hasData) {
            // Enable export buttons if there's data
            if (this.exportCSVButton) {
                this.exportCSVButton.removeAttribute('disabled');
                disabledClasses.forEach(cls => this.exportCSVButton.classList.remove(cls));
                enabledClasses.forEach(cls => this.exportCSVButton.classList.add(cls));
            }
            if (this.exportJSONButton) {
                this.exportJSONButton.removeAttribute('disabled');
                disabledClasses.forEach(cls => this.exportJSONButton.classList.remove(cls));
                enabledClasses.forEach(cls => this.exportJSONButton.classList.add(cls));
            }
        } else {
            // Disable export buttons if no data
            if (this.exportCSVButton) {
                this.exportCSVButton.setAttribute('disabled', '');
                enabledClasses.forEach(cls => this.exportCSVButton.classList.remove(cls));
                disabledClasses.forEach(cls => this.exportCSVButton.classList.add(cls));
            }
            if (this.exportJSONButton) {
                this.exportJSONButton.setAttribute('disabled', '');
                enabledClasses.forEach(cls => this.exportJSONButton.classList.remove(cls));
                disabledClasses.forEach(cls => this.exportJSONButton.classList.add(cls));
            }
        }
    }
    updateTimer(time) {
        if (this.timerDisplay) {
            this.timerDisplay.textContent = `Timer: ${time}`;
        }
    }
    //togle timer 
    updateButtonText() {
        if (this.toggleButton) {
            this.toggleButton.textContent = this.timerOn ? 'Stop' : 'Start';
            this.toggleButton.classList.remove(this.timerOn ? 'bg-purple-400' : 'bg-red-400');
            this.toggleButton.classList.add(this.timerOn ? 'bg-red-400' : 'bg-purple-400');
        }
    }

    toggleTimer() {
        if (this.timerOn) {
            this.timerManager.stopTimer();
            // timerOn will be set to false by resetTimerState
        } else {
            // const duration = parseInt(document.getElementById('duration').value);
            const interval = parseInt(document.getElementById('interval').value);
            console.log('Current interval value:', interval); // Debug log
            if (interval > 0) {
                this.timerOn = true;
                this.updateButtonText();
                this.timerManager.startTimer(interval);
                console.log('Starting timer with interval:', interval); // Debug log

            } else {
                alert('Please enter valid duration and interval values');
                return;
            }
        }
    }
    //toggle timer part end
    showMeasurementAlert() {
        if (this.measurementAlert) {
            this.measurementAlert.style.display = 'block';
            setTimeout(() => {
                this.measurementAlert.style.display = 'none';
            }, 5000);
        }
    }

    getDurationSetting() {
        return parseInt(this.durationInput?.value || '60');
    }

    getIntervalSetting() {
        const intervalValue = this.intervalInput?.value;
        console.log('Interval input value:', intervalValue); // Debug log
        return parseInt(this.intervalInput?.value || '5');
    }

    resetTimerState() {
        this.timerOn = false;
        this.updateButtonText();
        if (this.timerDisplay) {
            this.timerDisplay.textContent = 'Timer: 0s';
        }
        console.log('Timer state reset, timerOn:', this.timerOn);
    }
     //make body go full screen 
     toggleFullScreen() {
        const container = document.querySelector('.container');
        if (!document.fullscreenElement) {
            // Enter fullscreen
            if (container.requestFullscreen) {
                container.requestFullscreen().catch(err => {
                    console.error(`Error attempting to enable fullscreen: ${err.message}`);
                });
                // Add fullscreen attribute to html and body
                document.documentElement.setAttribute("fullscreen", "");
                document.body.setAttribute("fullscreen", "");
            }
        } else {
            // Exit fullscreen
            if (document.exitFullscreen) {
                document.exitFullscreen();
                // Remove fullscreen attribute from html and body
                document.documentElement.removeAttribute("fullscreen");
                document.body.removeAttribute("fullscreen");
            }
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
