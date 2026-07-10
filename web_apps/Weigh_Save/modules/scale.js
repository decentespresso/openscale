import { DataExport } from './export.js';
import { downloadFile } from '../../shared/modules/download-file.js';

export class DecentScale {
    constructor(uiController, timerManager) {
        this.uiController = uiController;
        this.timerManager = timerManager;
        this.weightReadings = [];
        this.weightData = [];
        this.readingCount = 0;
        this.lastWeight = 0;
        this.lastWeightTime = 0;
        this.stabilityThreshold = 0.2;
    }

    processWeight(weight) {
        this.uiController.updateWeightDisplay(weight);

        if (!this.timerManager || !this.timerManager.shouldTakeMeasurement()) {
            return;
        }

        const currentTime = Date.now();
        const weightTolerance = Math.abs(weight - this.lastWeight);

        if (
            weightTolerance < this.stabilityThreshold ||
            weight === this.lastWeight ||
            Math.abs(weight) <= this.stabilityThreshold
        ) {
            return;
        }

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
    }

    rate_grams_per_second(currentWeight, currentTime) {
        if (this.lastWeightTime === 0 || this.lastWeight === 0) {
            return 0;
        }
        return (currentWeight - this.lastWeight) / ((currentTime - this.lastWeightTime) / 1000);
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
}
