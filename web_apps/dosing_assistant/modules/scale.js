import { SCALE_CONSTANTS } from './constants.js';
import { DataExport } from './export.js';
import { downloadFile } from '../../shared/modules/download-file.js';

export class DecentScale {
    constructor(uiController, stateMachine) {
        this.uiController = uiController;
        this.stateMachine = stateMachine;
        this.weightReadings = [];
        this.weightData = [];
        this.containerWeight = 0;
        this.waitingForContainerWeight = false;
        this.dosingPausedForContainerRemoval = false;
        this.dosingMode = false;
        this.tareWeight = 0;
        this.currentProgress = 0;
        this.lastWeight = 0;
        this.doseSaved = false;
        this.stableWeightReadings = [];
        this.stabilityThreshold = 0.4;
        this.stabilityDuration = 2;
        this.doseCompletedAndSaved = false;
        this.readingCount = 0;
        this.soundplayed = false;
        this.soundEnabled = true;
        this.ws = null;
    }

    tare() {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send('tare');
            return;
        }
        alert('WebSocket not connected.');
    }

    handleWebSocketWeight(weight) {
        const netWeight = weight - (this.tareWeight || 0);

        if (this.uiController) {
            this.uiController.updateWeightDisplay(netWeight);
        }

        this.stableWeightReadings.push(netWeight);
        if (this.stableWeightReadings.length > this.stabilityDuration) {
            this.stableWeightReadings.shift();
        }

        if (this.dosingMode) {
            this.handleDosingMode(netWeight);
        }
    }

    handleDosingMode(netWeight) {
        const target = this.dosingSettings?.targetWeight || 0;
        const lowThreshold = this.dosingSettings?.lowThreshold || 0;
        const highThreshold = this.dosingSettings?.highThreshold || 0;

        if (!this.dosingSettings) {
            console.error('Dosing settings not configured');
            return;
        }

        this.currentProgress = Math.min((netWeight / target) * 100, 100);
        this.uiController.updateProgressBar(this.currentProgress);

        if (netWeight >= lowThreshold && netWeight <= highThreshold) {
            this.uiController.updateProgressBarColor('success');
        } else if (netWeight < lowThreshold) {
            this.uiController.updateProgressBarColor('warning');
        } else if (netWeight > highThreshold) {
            this.uiController.updateProgressBarColor('error');
        }

        this.stateMachine.handleWeightUpdate(netWeight, this);
    }

    toggleDosingMode() {
        if (this.dosingMode) {
            this.dosingMode = false;
            this.uiController.updateGuidance('Dosing Stopped');
            this.uiController.updateDosingButton('Start', false);
            this.uiController.hideSetContainerWeightButton();
            this.uiController.disableSetContainerWeightButton();
            this.uiController.updateProgressBarColor('default');
            this.currentProgress = 0;
            this.doseSaved = false;
            this.stopDosing(true);
            return;
        }

        this.doseSaved = false;
        this.doseCompletedAndSaved = false;
        this.currentProgress = 0;
        this.stableWeightReadings = [];
        this.uiController.updateDosingButton('Stop', true);
        this.uiController.updateGuidance('Place container on scale and click set container weight.');
        this.uiController.showSetContainerWeightButton();
        this.uiController.enableSetContainerWeightButton();
        this.waitingForContainerWeight = true;
    }

    startDosingAutomatically() {
        if (this.dosingMode) {
            return;
        }

        this.dosingMode = true;
        this.doseCompletedAndSaved = false;
        this.currentProgress = 0;
        this.stableWeightReadings = [];
        this.doseSaved = false;
        this.uiController.updateProgressBar(0);
        this.uiController.updateProgressBarColor('default');
        this.uiController.updateDosingButton('Stop', true);
        this.uiController.updateGuidance('Dosing automatically resumed. Ready for next measurement.', 'success');
        this.stateMachine.setCurrentState(SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT);
    }

    stopDosing(manualStop = true) {
        this.dosingMode = false;
        this.currentProgress = 0;

        if (manualStop) {
            this.dosingPausedForContainerRemoval = false;
        }

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
        this.uiController.enableStartDosingButton();
        this.uiController.disableSetContainerWeightButton();
        this.configureDosingSettings();
        this.waitingForContainerWeight = false;
    }

    configureDosingSettings() {
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
        this.tare();
        this.tareWeight = 0;
        this.dosingMode = true;
        this.stateMachine.setCurrentState(SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT);
        this.uiController.updateDosingButton('Stop', true);
        this.uiController.updateGuidance('Ready! Place object on scale');
    }

    saveDosing(finalWeight) {
        this.readingCount++;
        const reading = {
            readings: this.readingCount,
            timestamp: new Date().toLocaleString('en-GB'),
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
    }

    getDoseStatus(weight) {
        const lowThreshold = this.dosingSettings.lowThreshold;
        const highThreshold = this.dosingSettings.highThreshold;

        if (weight < lowThreshold) {
            return 'Not enough weight.';
        }
        if (weight > highThreshold) {
            return 'Weight exceeded.';
        }
        return weight === this.dosingSettings.targetWeight ? 'OK' : 'PASS';
    }

    isWeightStable() {
        if (this.stableWeightReadings.length < this.stabilityDuration) {
            return false;
        }

        const maxWeight = Math.max(...this.stableWeightReadings);
        const minWeight = Math.min(...this.stableWeightReadings);
        return maxWeight - minWeight <= this.stabilityThreshold;
    }

    playSound(type) {
        if (this.soundplayed || !this.soundEnabled) {
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
            } else {
                oscillator.type = 'sawtooth';
                oscillator.frequency.setValueAtTime(400, audioContext.currentTime);
                gainNode.gain.setValueAtTime(0.2, audioContext.currentTime);
                oscillator.frequency.linearRampToValueAtTime(50, audioContext.currentTime + 0.2);
            }

            this.soundplayed = true;
            oscillator.connect(gainNode);
            gainNode.connect(audioContext.destination);
            oscillator.start();

            setTimeout(() => {
                oscillator.stop();
                audioContext.close();
                this.soundplayed = false;
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
            downloadFile(result.content, result.filename, result.type);
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
            downloadFile(result.content, result.filename, result.type);
        } catch (error) {
            console.error('JSON export error:', error);
            alert('Export failed. See console for details.');
        }
    }

    updateSoundPreference(enabled) {
        this.soundEnabled = enabled;
    }
}
