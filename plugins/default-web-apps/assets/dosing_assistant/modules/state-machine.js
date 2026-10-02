import { SCALE_CONSTANTS } from './constants.js';

export class StateMachine {
    constructor(uiController) {
        this.currentState = SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT;
        this.removalTimeout = null;
        this.removalTimeoutDuration = 5000;
        this.uiController = uiController;
        this.stabilityWindow = null;
    }

    setCurrentState(state) {
        this.stabilityWindow = null;
        this.currentState = state;
    }

    handleWeightUpdate(netWeight, scale) {
        switch (this.currentState) {
            case SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT:
                this.handleWaitingState(netWeight, scale);
                break;

            case SCALE_CONSTANTS.FSM_STATES.MEASURING:
                this.handleMeasuringState(netWeight, scale);
                break;

            case SCALE_CONSTANTS.FSM_STATES.REMOVAL_PENDING:
                this.handleRemovalPendingState(netWeight, scale);
                break;

            case SCALE_CONSTANTS.FSM_STATES.CONTAINER_REMOVED:
                this.handleContainerRemovedState(netWeight, scale);
                break;
        }
    }

    handleWaitingState(netWeight, scale) {
        if (Math.abs(netWeight) <= SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE) {
            scale.stableWeightReadings = [];
            this.uiController.updateGuidance('Place object on scale', 'info');
            this.currentState = SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT;
        } else if (netWeight > SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE) {
            this.currentState = SCALE_CONSTANTS.FSM_STATES.MEASURING;
            this.uiController.updateGuidance('Measuring in progress...', 'info');
        }
    }

    handleMeasuringState(netWeight, scale) {
        if (netWeight < -SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE) {
            this.stabilityWindow = null;
            console.log('Container removed - negative weight detected:', netWeight);
            this.currentState = SCALE_CONSTANTS.FSM_STATES.CONTAINER_REMOVED;
            scale.dosingPausedForContainerRemoval = true;
            scale.stableWeightReadings = [];
            this.uiController.updateGuidance('Container removed, please replace it', 'warning');
            return;
        }

        if (Math.abs(netWeight) <= SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE) {
            this.setCurrentState(SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT);
            scale.stableWeightReadings = [];
            scale.weightIsStable = false;
            return;
        }

        if (!this.checkWeightStability(scale.stableWeightReadings)) {
            this.stabilityWindow = null;
            scale.weightIsStable = false;
            scale.doseSaved = false;
            this.uiController.updateGuidance('Stabilizing...', 'info');
            return;
        }

        const now = Date.now();
        const previous = this.stabilityWindow;
        const minimum = Math.min(previous?.minimum ?? netWeight, netWeight);
        const maximum = Math.max(previous?.maximum ?? netWeight, netWeight);
        if (!previous || now - previous.lastSampleAt > 1000 ||
            now < previous.lastSampleAt || maximum - minimum > 0.4) {
            this.stabilityWindow = {minimum: netWeight, maximum: netWeight,
                startedAt: now, lastSampleAt: now};
            scale.weightIsStable = false;
            return;
        }
        this.stabilityWindow = {...previous, minimum, maximum, lastSampleAt: now};
        if (now - previous.startedAt < 2500 || scale.doseSaved) return;

        scale.weightIsStable = true;
        const {targetWeight, lowThreshold, highThreshold} = scale.dosingSettings;
        const remaining = targetWeight - netWeight;
        scale.saveDosing(netWeight);
        scale.doseSaved = true;
        if (netWeight >= lowThreshold && netWeight <= highThreshold) {
            this.uiController.updateGuidance('Target weight reached!', 'success');
            this.uiController.updateProgressBarColor('success');
            scale.playSound('pass');
        } else if (netWeight < lowThreshold) {
            this.uiController.updateGuidance(`Failed: Under target by ${remaining.toFixed(1)}g`, 'warning');
            this.uiController.updateProgressBarColor('warning');
            scale.playSound('fail');
        } else {
            this.uiController.updateGuidance(`Failed: Over target by ${(-remaining).toFixed(1)}g`, 'error');
            this.uiController.updateProgressBarColor('error');
            scale.playSound('fail');
        }
        this.setCurrentState(SCALE_CONSTANTS.FSM_STATES.REMOVAL_PENDING);
    }

    handleRemovalPendingState(netWeight, scale) {
        console.log('handleRemovalPendingState:', {
            netWeight,
            zeroTolerance: SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE
        });

        if (Math.abs(netWeight) <= SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE) {
            scale.doseSaved = false;
            scale.stableWeightReadings = [];
            this.currentState = SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT;

            scale.tare();
            console.log("Tare called - weight near zero");
            this.uiController.updateGuidance('Ready! Place next object on scale', 'success');
        } else if (netWeight > SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE) {
            this.uiController.updateGuidance('Measurement Done - Put the next object on', 'warning');
        }
    }

    handleContainerRemovedState(netWeight, scale) {
        console.log("handleContainerRemovedState");
        if (netWeight >= -SCALE_CONSTANTS.WEIGHT_THRESHOLDS.CONTAINER_TOLERANCE &&
            netWeight <= SCALE_CONSTANTS.WEIGHT_THRESHOLDS.CONTAINER_TOLERANCE) {
            console.log('Container put back on, resuming dosing');
            scale.dosingPausedForContainerRemoval = false;

            scale.startDosingAutomatically();

            this.uiController.updateGuidance('Container back on scale, ready for next dose', 'success');
            this.currentState = SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT;
        }
        else if (netWeight>SCALE_CONSTANTS.NEW_CONTAINER_TOLERANCE){
            this.uiController.updateGuidance('New container detected, click zero to continue', 'warning');
        }
    }

    checkWeightStability(stableWeightReadings, threshold = 0.4, minReadings = 2) {
        if (!stableWeightReadings || stableWeightReadings.length < minReadings) {
            console.log('Weight Stability: Not enough readings yet.');
            return false;
        }

        const recentReadings = stableWeightReadings.slice(-minReadings);

        const isNearZero = recentReadings.every(weight =>
            Math.abs(weight) <= SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE
        );

        if (isNearZero) {
            console.log('Weight Stability: Readings near zero - considering noise');
            return false;
        }

        const maxWeight = Math.max(...recentReadings);
        const minWeight = Math.min(...recentReadings);
        const weightDifference = maxWeight - minWeight;

        const isStable = weightDifference <= threshold;

        console.log('Weight Stability Check:', {
            readings: recentReadings,
            maxWeight: maxWeight.toFixed(2),
            minWeight: minWeight.toFixed(2),
            difference: weightDifference.toFixed(2),
            threshold: threshold,
            isStable: isStable,
            isNearZero: isNearZero
        });

        return isStable;
    }
}
