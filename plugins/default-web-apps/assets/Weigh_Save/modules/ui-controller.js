export class UIController {
    constructor(timerManager) {
        this.timerManager = timerManager;
        this.timerDisplay = document.getElementById('timer');
        this.toggleButton = document.getElementById('toggleTimer');
        this.timerOn = false;
        this.weightDisplay = document.getElementById('weight');
        this.statusDisplay = document.getElementById('status');
        this.measurementAlert = document.getElementById('measurementAlert');
        this.weightReadingsList = document.getElementById('weightReadings');

        this.exportCSVButton = document.getElementById('exportCSV');
        this.exportJSONButton = document.getElementById('exportJSON');
        this.rateDisplay = document.getElementById('rate');
        this.fullscreenButton = document.getElementById('fullscreen-button');
        this.setupFullscreenHandler();
        console.log('UI Controller initialized with elements:', {
            weightDisplay: !!this.weightDisplay,
            statusDisplay: !!this.statusDisplay,
            weightReadingsList: !!this.weightReadingsList
        });
    }

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

    updateStatus(message) {
        if (this.statusDisplay) {
            this.statusDisplay.textContent = `Status: ${message}`;
        }
    }

    displayWeightReadings(weightReadings) {
        if (!this.weightReadingsList) return;

        this.weightReadingsList.innerHTML = '';
        const hasData = Array.isArray(weightReadings) && weightReadings.length > 0;

        if (hasData) {
            const table = document.createElement('table');
            table.className = 'w-full border-collapse';

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
                const [readingNum, weight, rateWithParens, ...timestampParts] = reading.split(',').map(s => s.trim());

                const rate = rateWithParens ? rateWithParens.replace(/[()]/g, '').replace(/"/g, '') : '';

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

    updateExportButtonStates(hasData) {
        console.log('Updating export buttons state:', hasData ? 'enabled' : 'disabled');

        const enabledClasses = ['bg-purple-100', 'text-purple-800', 'border-purple-300', 'hover:bg-purple-200'];
        const disabledClasses = ['bg-gray-50', 'opacity-50', 'cursor-not-allowed'];

        for (const button of [this.exportCSVButton, this.exportJSONButton].filter(Boolean)) {
            button.disabled = !hasData;
            enabledClasses.forEach(cls => button.classList.toggle(cls, hasData));
            disabledClasses.forEach(cls => button.classList.toggle(cls, !hasData));
        }
    }
    updateTimer(time) {
        if (this.timerDisplay) {
            this.timerDisplay.textContent = `Timer: ${time}`;
        }
    }
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
        } else {
            const interval = parseInt(document.getElementById('interval').value);
            console.log('Current interval value:', interval);
            if (interval > 0) {
                this.timerOn = true;
                this.updateButtonText();
                this.timerManager.startTimer(interval);
                console.log('Starting timer with interval:', interval);

            } else {
                alert('Please enter valid duration and interval values');
                return;
            }
        }
    }
    showMeasurementAlert() {
        if (this.measurementAlert) {
            this.measurementAlert.style.display = 'block';
            setTimeout(() => {
                this.measurementAlert.style.display = 'none';
            }, 5000);
        }
    }

    resetTimerState() {
        this.timerOn = false;
        this.updateButtonText();
        if (this.timerDisplay) {
            this.timerDisplay.textContent = 'Timer: 0s';
        }
        console.log('Timer state reset, timerOn:', this.timerOn);
    }
     toggleFullScreen() {
        const container = document.querySelector('.container');
        if (!document.fullscreenElement) {
            if (container.requestFullscreen) {
                container.requestFullscreen().catch(err => {
                    console.error(`Error attempting to enable fullscreen: ${err.message}`);
                });
                document.documentElement.setAttribute("fullscreen", "");
                document.body.setAttribute("fullscreen", "");
            }
        } else {
            if (document.exitFullscreen) {
                document.exitFullscreen();
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
