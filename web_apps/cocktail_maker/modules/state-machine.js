import { SCALE_CONSTANTS } from './constants.js';
import { DecentScale } from './scale.js';

export class StateMachine {
    constructor(uiController) {
        this.currentState = SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT;
        this.removalTimeout = null;
        this.removalTimeoutDuration = 5000; // 5 seconds
        this.uiController = uiController; // Store UI controller reference
        this.weightStableTimeout = null; // Timeout for stable weight
    }

    setCurrentState(state) {
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
            scale.stableWeightReadings = []; // Clear stability readings
            this.currentState = SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT;
        } else if (netWeight > SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE) {
            this.currentState = SCALE_CONSTANTS.FSM_STATES.MEASURING;
            this.uiController.updateGuidance('Measuring...', 'info');
        }
    }

    handleMeasuringState(netWeight, scale) {
        const isStable = this.checkWeightStability(scale.stableWeightReadings);
        const { targetWeight, lowThreshold, highThreshold } = scale.dosingSettings;
        const remaining = targetWeight - netWeight;

        if (isStable) {
            // Weight is stable.
            if (scale.doseSaved) {
                // Already completed this step, waiting for cocktail.js to advance.
                return;
            }

            if (netWeight >= lowThreshold && netWeight <= highThreshold) {
                // Weight is stable and in range. This step is complete.
                console.log(`Weight stable in target range: ${netWeight}g. Transitioning.`);
                scale.doseSaved = true;
                // The transition to REMOVAL_PENDING will trigger the next step in cocktail.js
                this.currentState = SCALE_CONSTANTS.FSM_STATES.REMOVAL_PENDING;
            } else if (netWeight > highThreshold) {
                // Stable, but too high.
                this.uiController.updateGuidance(`Over by ${(netWeight - targetWeight).toFixed(1)}g. Please remove some.`, 'error');
            } else if (netWeight > SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE) {
                // Stable, but too low.
                this.uiController.updateGuidance(`Add ${remaining.toFixed(1)}g more`, 'info');
            }
        } else {
            // Weight is not stable, which means user is likely pouring.
            scale.doseSaved = false; // Reset saved flag if weight becomes unstable
            if (remaining > 0) {
                this.uiController.updateGuidance(`Add ${remaining.toFixed(1)}g more`, 'info');
            } else {
                // User is pouring and already over the target
                this.uiController.updateGuidance(`Pouring... Over by ${(-remaining).toFixed(1)}g`, 'warning');
            }
        }
    }

    handleRemovalPendingState(netWeight, scale) {
        // This state is now just a trigger for cocktail.js.
        // The logic in cocktail.js will tare the scale and advance to the next step.
        // We don't need to do anything here, the monkey-patch will handle it.
        return;
    }

    handleContainerRemovedState(netWeight, scale) {
        // Check if weight is back within container tolerance
        console.log("handleContainerRemovedState");
        if (netWeight >= -SCALE_CONSTANTS.WEIGHT_THRESHOLDS.CONTAINER_TOLERANCE && 
            netWeight <= SCALE_CONSTANTS.WEIGHT_THRESHOLDS.CONTAINER_TOLERANCE) {
            console.log('Container put back on, resuming dosing');
            scale.dosingPausedForContainerRemoval = false;
            
            // Resume dosing automatically
            scale.startDosingAutomatically();
            
            // Update UI and state
            this.uiController.updateGuidance('Container back on scale, ready for next dose', 'success');
            this.currentState = SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT;
        }
        else if (netWeight>SCALE_CONSTANTS.NEW_CONTAINER_TOLERANCE){
            this.uiController.updateGuidance('New container detected, click zero to continue', 'warning');
        }
    }
    
    checkWeightStability(stableWeightReadings, threshold = 0.4, minReadings = 4) {
        // Check if we have enough readings
        if (!stableWeightReadings || stableWeightReadings.length < minReadings) {
            return false;
        }

        // Get the last n readings
        const recentReadings = stableWeightReadings.slice(-minReadings);
        
        // Calculate max and min from recent readings
        const maxWeight = Math.max(...recentReadings);
        const minWeight = Math.min(...recentReadings);
        const weightDifference = maxWeight - minWeight;
        
        const isStable = weightDifference <= threshold;

        return isStable;
    }
}
