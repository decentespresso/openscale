import { SCALE_CONSTANTS } from './constants.js';

export class StateMachine {
    constructor(uiController) {
        this.currentState = SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT;
        this.removalTimeout = null;
        this.removalTimeoutDuration = 5000; // 5 seconds
        this.uiController = uiController; // Store UI controller reference
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
        if (netWeight == 0) {
            console.log('No weight detected, transitioning to WAITING_FOR_NEXT');
            this.uiController.updateGuidance('Place object on scale', 'info');
            this.currentState = SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT;
        } else if (netWeight >= SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE) {
            console.log('State: WAITING_FOR_NEXT. New object detected, transitioning to MEASURING');
            this.uiController.updateGuidance('Measuring in progress...', 'info');
            this.currentState = SCALE_CONSTANTS.FSM_STATES.MEASURING;
            scale.weightIsStable = false;
            scale.lastWeight = netWeight;
        } else if (netWeight < SCALE_CONSTANTS.WEIGHT_THRESHOLDS.MINIMUM) {
            console.log('Container removed, pausing dosing mode');
            scale.dosingPausedForContainerRemoval = true;
            scale.stopDosing(false); // Pass false to indicate it's not a manual stop
            this.currentState = SCALE_CONSTANTS.FSM_STATES.CONTAINER_REMOVED;
            this.uiController.updateGuidance('Container removed, dosing paused', 'warning');
            console.log('Current Flags at WAITING_FOR_NEXT state end, moved to container removed state');
        }
    }

    handleMeasuringState(netWeight, scale) {
        const isStable = this.checkWeightStability(scale.stableWeightReadings);
        
        if (isStable) {
            scale.weightIsStable = true;
            
            const target = scale.dosingSettings.targetWeight;
            const lowThreshold = scale.dosingSettings.lowThreshold;
            const highThreshold = scale.dosingSettings.highThreshold;
            
            const remaining = target - netWeight;
            
            // Update UI based on weight
            if (netWeight >= lowThreshold && netWeight <= highThreshold) {
                console.log('Weight in target range:', netWeight);
                
                // Only save if not already saved
                if (!scale.doseSaved) {
                    scale.saveDosing(netWeight);
                    scale.doseSaved = true; // Mark as saved
                    this.uiController.updateGuidance('Target weight reached!', 'success');
                    this.uiController.updateProgressBarColor('success');
                    
                    // Set timeout for container removal
                    if (!this.removalTimeout) {
                        this.currentState = SCALE_CONSTANTS.FSM_STATES.REMOVAL_PENDING;
                        this.uiController.updateGuidance('Target reached! Remove container.', 'success');
                        
                        this.removalTimeout = setTimeout(() => {
                            console.log('Removal timeout expired, resetting to waiting state');
                            this.currentState = SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT;
                            this.uiController.updateGuidance('Ready for next measurement', 'info');
                            this.removalTimeout = null;
                        }, this.removalTimeoutDuration);
                    }
                }
            } else {
                // Reset the saved flag if weight goes out of range
                scale.doseSaved = false;
                if (netWeight < 0) {
                    console.log('Container removed, pausing dosing mode');
                    scale.dosingPausedForContainerRemoval = true;
                    this.uiController.updateProgressBarColor('error');
                    this.currentState = SCALE_CONSTANTS.FSM_STATES.CONTAINER_REMOVED;
                    this.uiController.updateGuidance('Container removed, dosing paused', 'warning');
                } else if (netWeight < lowThreshold) {
                    console.log('Weight below low threshold:', netWeight);
                    this.uiController.updateGuidance(`Add ${remaining.toFixed(1)}g more`, 'info');
                    this.uiController.updateProgressBarColor('warning');
                } else if (netWeight > highThreshold) {
                    console.log('Weight above high threshold:', netWeight);
                    this.uiController.updateGuidance(`Remove ${(-remaining).toFixed(1)}g`, 'error');
                    this.uiController.updateProgressBarColor('error');
                }
            }
        } else {
            // Weight not stable
            scale.weightIsStable = false;
            console.log('Weight not stable yet:', netWeight);
            this.uiController.updateGuidance('Stabilizing...', 'info');
        }
        
        scale.lastWeight = netWeight;
    }

    handleRemovalPendingState(netWeight, scale) {
        if (netWeight > SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE) {
            // Object still on scale
            this.uiController.updateGuidance('Remove this object, and then place the next object', 'warning');
        } else if (Math.abs(netWeight) <= SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE) {
            // Weight is close to zero - object removed
            this.currentState = SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT;
            this.uiController.updateGuidance('Ready! Place next object on scale', 'success');
            
            if (scale.dosingPausedForContainerRemoval) {
                scale.startDosingAutomatically();
            }
        }
    }

    handleContainerRemovedState(netWeight, scale) {
        // Check if weight is back within container tolerance
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
    }
    
    checkWeightStability(stableWeightReadings, threshold = 0.2, minReadings = 4) {
        // Check if we have enough readings
        if (!stableWeightReadings || stableWeightReadings.length < minReadings) {
            console.log('Weight Stability: Not enough readings yet.');
            return false;
        }

        // Get the last n readings
        const recentReadings = stableWeightReadings.slice(-minReadings);
        
        // Calculate max and min from recent readings
        const maxWeight = Math.max(...recentReadings);
        const minWeight = Math.min(...recentReadings);
        const weightDifference = maxWeight - minWeight;
        
        // Check if weight is stable
        const isStable = weightDifference <= threshold;

        console.log('Weight Stability Check:', {
            readings: recentReadings,
            maxWeight: maxWeight.toFixed(2),
            minWeight: minWeight.toFixed(2),
            difference: weightDifference.toFixed(2),
            threshold: threshold,
            isStable: isStable
        });

        return isStable;
    }
}
