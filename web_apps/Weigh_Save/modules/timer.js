export class TimerManager {
    constructor() {
        this.uiController = null;
        this.timerInterval = null;
        this.elapsedTime = 0;
        this.measurementInterval = null;
        this.lastWeightReadingTime = 0;
    }

    setUIController(uiController) {
        this.uiController = uiController;
    }

    startTimer( interval) {
        if (!this.uiController) {
        console.error('UIController not set');
        return;
    }

    console.log('Starting timer with interval:', interval);
    this.elapsedTime = 0;
    this.measurementInterval = interval * 1000;
    this.lastWeightReadingTime = 0;
    
    clearInterval(this.timerInterval);
    this.timerInterval = setInterval(() => {
        this.elapsedTime++;
        this.uiController.updateTimer(this.elapsedTime);

        // if (this.elapsedTime >= duration) {
        //     this.stopTimer();
        // }
    }, 1000);
    }

    stopTimer() {
        console.log('Stopping timer');
        clearInterval(this.timerInterval);
        this.timerInterval = null;
        this.uiController.showMeasurementAlert();
        // Reset UI state when timer stops
        this.uiController.resetTimerState();
    }

    shouldTakeMeasurement() {
        const currentTime = Date.now();
        if (this.timerInterval && (currentTime - this.lastWeightReadingTime) >= this.measurementInterval) {
            console.log("Should take measurement - Time elapsed:", currentTime - this.lastWeightReadingTime);
            this.lastWeightReadingTime = currentTime;
            return true;
        }
        return false;
    }
}