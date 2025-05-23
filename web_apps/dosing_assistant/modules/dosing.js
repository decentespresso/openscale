import { SCALE_CONSTANTS } from './modules/constants.js';

export class DosingManager {
    constructor(uiController) {
        this.ui = uiController;
        this.dosingMode = false;
        this.settings = {
            targetWeight: 0,
            lowThreshold: 0,
            highThreshold: 0
        };
        this.currentProgress = 0;
        this.readingCount = 0;
        this.weightData = [];
        this.weightReadings = [];
        this.containerWeight = 0;
        this.waitingForContainerWeight = false;
    }

    startDosing() {
        this.dosingMode = true;
        this.settings = {
            targetWeight: parseFloat(this.ui.getTargetWeight()),
            lowThreshold: parseFloat(this.ui.getLowThreshold()),
            highThreshold: parseFloat(this.ui.getHighThreshold())
        };
        this.currentProgress = 0;
        this.waitingForContainerWeight = true;
        
        // Update UI
        this.ui.updateProgressBar(0);
        this.ui.updateProgressBarColor('default');
        this.ui.updateGuidance('Place container on scale and click set container weight.');
        this.ui.showSetContainerWeightButton();
        
        console.log('Dosing started with settings:', this.settings);
    }

    stopDosing() {
        this.dosingMode = false;
        this.currentProgress = 0;
        this.waitingForContainerWeight = false;
        
        // Update UI
        this.ui.updateProgressBar(0);
        this.ui.updateProgressBarColor('default');
        this.ui.updateGuidance('Dosing stopped');
        this.ui.hideSetContainerWeightButton();
        
        console.log('Dosing stopped');
    }

    handleWeightUpdate(weight, netWeight) {
        if (!this.dosingMode) return;

        // If waiting for container weight, don't update progress
        if (this.waitingForContainerWeight) {
            this.ui.updateGuidance('Set container weight to continue');
            return;
        }

        // Calculate progress based on net weight
        const target = this.settings.targetWeight;
        const progress = Math.min((netWeight / target) * 100, 100);
        this.currentProgress = progress;

        // Update UI
        this.ui.updateProgressBar(progress);
        this.updateProgressColor(netWeight);

        console.log('Weight update:', {
            netWeight,
            progress: progress.toFixed(1) + '%',
            target
        });
    }

    updateProgressColor(netWeight) {
        if (netWeight >= this.settings.lowThreshold && 
            netWeight <= this.settings.highThreshold) {
            this.ui.updateProgressBarColor('success');
            this.ui.updateGuidance('Perfect! Remove container');
        } else if (netWeight > this.settings.highThreshold) {
            this.ui.updateProgressBarColor('error');
            this.ui.updateGuidance(`Remove ${(netWeight - this.settings.targetWeight).toFixed(1)}g`);
        } else {
            this.ui.updateProgressBarColor('warning');
            this.ui.updateGuidance(`Add ${(this.settings.targetWeight - netWeight).toFixed(1)}g more`);
        }
    }

    setContainerWeight(weight) {
        this.containerWeight = weight;
        this.waitingForContainerWeight = false;
        this.ui.updateGuidance('Container weight set. Ready for dosing!');
        this.ui.hideSetContainerWeightButton();
        console.log('Container weight set to:', weight);
    }

    saveMeasurement(weight) {
        this.readingCount++;
        const reading = {
            readings: this.readingCount,
            timestamp: new Date().toLocaleString('en-GB'),
            weight: weight.toFixed(1),
            target: this.settings.targetWeight,
            lowThreshold: this.settings.lowThreshold,
            highThreshold: this.settings.highThreshold,
            status: this.evaluateWeight(weight)
        };

        this.weightData.push(reading);
        this.weightReadings.push(
            `${reading.readings}. ${reading.timestamp}: ${reading.weight}g / ${reading.target}g - ${reading.status.toUpperCase()}`
        );
        
        this.ui.displayWeightReadings(this.weightReadings);
        return reading;
    }

    evaluateWeight(weight) {
        return (weight >= this.settings.lowThreshold && 
                weight <= this.settings.highThreshold) ? 'pass' : 'fail';
    }

    isDosing() {
        return this.dosingMode;
    }

    isWaitingForContainer() {
        return this.waitingForContainerWeight;
    }

    getWeightData() {
        return this.weightData;
    }

    getWeightReadings() {
        return this.weightReadings;
    }
}
