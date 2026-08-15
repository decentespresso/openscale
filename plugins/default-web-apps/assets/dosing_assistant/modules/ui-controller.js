export class UIController {
    constructor() {
        // Initialize UI elements
        this.weightDisplay = document.getElementById('weight');
        this.statusDisplay = document.getElementById('status');
        this.guidanceDisplay = document.getElementById('guidance');
        this.progressFill = document.getElementById('progressFill');
        this.progressPercent = document.getElementById('progressPercent');
        this.weightReadingsList = document.getElementById('weightReadings');
        this.setContainerWeightButton = document.getElementById('setContainerWeightButton');
        this.connectButton = document.getElementById('connectButton');
        this.dosingToggleButton = document.getElementById('dosingToggleButton');
        this.exportCSVButton = document.getElementById('exportCSV');
        this.exportJSONButton = document.getElementById('exportJSON');

        // Add input elements
        this.targetWeightInput = document.getElementById('targetWeight');
        this.lowThresholdInput = document.getElementById('lowThreshold');
        this.highThresholdInput = document.getElementById('highThreshold');
        
        // Add sound preference checkbox
        this.soundToggle = document.getElementById('soundEnabled');
        
        // Initialize input listeners
        this.setupTargetWeightListener();
        //fullscreenButton
        this.fullscreenButton = document.getElementById('fullscreen-button');
        this.setupFullscreenHandler();
        this.setupSoundToggle();

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

    // Add this new method
    setupSoundToggle() {
        if (this.soundToggle) {
            // Load saved preference
            const savedPreference = localStorage.getItem('soundEnabled');
            if (savedPreference !== null) {
                this.soundToggle.checked = savedPreference === 'true';
            }

            // Add change listener
            this.soundToggle.addEventListener('change', (e) => {
                const isEnabled = e.target.checked;
                localStorage.setItem('soundEnabled', isEnabled);
                // Notify scale instance
                if (this.scale) {
                    this.scale.updateSoundPreference(isEnabled);
                }
            });
        }
    }

    // Add method to set scale reference
    setScale(scale) {
        this.scale = scale;
        // Update initial sound preference
        if (this.soundToggle) {
            this.scale.updateSoundPreference(this.soundToggle.checked);
        }
    }

    updateSoundUI(enabled) {
        if (this.soundToggle) {
            this.soundToggle.checked = enabled;
        }
    }

    // Weight display methods
    updateWeightDisplay(weight) {
        if (this.weightDisplay) {
            this.weightDisplay.textContent = `Weight: ${weight.toFixed(1)} g`;
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
        
        // Clear and update weight readings list
        this.weightReadingsList.innerHTML = '';

        // Check if we have valid data
        const hasData = Array.isArray(weightReadings) && weightReadings.length > 0;
        
        if (hasData) {
            // Create reversed array to show newest first
            const reversedReadings = [...weightReadings].reverse();
            
            reversedReadings.forEach(reading => {
                const li = document.createElement('li');
                li.textContent = reading;
                li.className = 'py-1 px-2 border-b border-gray-200';
                this.weightReadingsList.appendChild(li);
            });
            
            console.log(`Updated weight readings display with ${weightReadings.length} entries (newest first)`);
        }

        // Update export button states
        this.updateExportButtonStates(hasData);
    }

    // Button control methods
    updateConnectButton(text) {
        if (this.connectButton) {
            this.connectButton.textContent = text;
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
