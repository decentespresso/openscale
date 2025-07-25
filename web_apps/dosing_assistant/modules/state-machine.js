import { SCALE_CONSTANTS } from './constants.js';
import { DecentScale } from './scale.js';
//the main core for dosing assistant logic is a Finite-state machine , below you will 4 states - Waiting for next , Measuring, removal pending, container removed. 
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
            this.uiController.updateGuidance('Place object on scale', 'info');
            this.currentState = SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT;
        } else if (netWeight > SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE) {
            this.currentState = SCALE_CONSTANTS.FSM_STATES.MEASURING;
            this.uiController.updateGuidance('Measuring in progress...', 'info');
        }
    }

    handleMeasuringState(netWeight, scale) {
        // First check for negative weight
        if (netWeight < -SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE) {
            console.log('Container removed - negative weight detected:', netWeight);
            this.currentState = SCALE_CONSTANTS.FSM_STATES.CONTAINER_REMOVED;
            scale.dosingPausedForContainerRemoval = true;
            scale.stableWeightReadings = []; // Clear readings
            this.uiController.updateGuidance('Container removed, please replace it', 'warning');
            return;
        }

        const isStable = this.checkWeightStability(scale.stableWeightReadings);
        console.log("isStable=", isStable);
        if (isStable) {
            scale.weightIsStable = true;
            
            // Start stability timeout if not already started
            if (!this.weightStableTimeout) {
                console.log('Weight stable - starting timeout');
                this.weightStableTimeout = setTimeout(() => {
                    if (!scale.doseSaved && this.checkWeightStability(scale.stableWeightReadings)) {
                        // Weight remained stable - use existing evaluation logic
                        const target = scale.dosingSettings.targetWeight;
                        const lowThreshold = scale.dosingSettings.lowThreshold;
                        const highThreshold = scale.dosingSettings.highThreshold;
                        const remaining = target - netWeight;

                        scale.saveDosing(netWeight);
                        scale.doseSaved = true;

                        if (netWeight >= lowThreshold && netWeight <= highThreshold) {
                            console.log('Weight stable - success');
                            this.uiController.updateGuidance('Target weight reached!', 'success');
                            this.uiController.updateProgressBarColor('success');
                            scale.playSound('pass'); // Play success sound
                        } else {
                            if (netWeight < lowThreshold) {
                                this.uiController.updateGuidance(`Failed: Under target by ${remaining.toFixed(1)}g`, 'warning');
                                this.uiController.updateProgressBarColor('warning');
                                console.log('Under Weight - fail');
                                scale.playSound('fail'); // Play fail sound
                            } else {
                                console.log('Over Weight - fail');
                                this.uiController.updateGuidance(`Failed: Over target by ${(-remaining).toFixed(1)}g`, 'error');
                                this.uiController.updateProgressBarColor('error');
                                scale.playSound('fail'); // Play fail sound
                            }
                        }
                        
                        this.currentState = SCALE_CONSTANTS.FSM_STATES.REMOVAL_PENDING;
                    }
                    this.weightStableTimeout = null;
                }, 2500);
            }
        } else {
            // Clear timeout if weight becomes unstable
            if (this.weightStableTimeout) {
                clearTimeout(this.weightStableTimeout);
                this.weightStableTimeout = null;
            }
            scale.weightIsStable = false;
            scale.doseSaved = false;
            this.uiController.updateGuidance('Stabilizing...', 'info');
        }
    }

    handleRemovalPendingState(netWeight, scale) {
        console.log('handleRemovalPendingState:', {
            netWeight,
            zeroTolerance: SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE
        });

        // Add stricter weight check
        if (Math.abs(netWeight) <= SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE) {
            // Reset for next measurement
            scale.doseSaved = false;
            scale.stableWeightReadings = []; // Clear stability readings
            this.currentState = SCALE_CONSTANTS.FSM_STATES.WAITING_FOR_NEXT;
            
            // Force tare when weight is near zero
            scale.tare();
            console.log("Tare called - weight near zero");
            this.uiController.updateGuidance('Ready! Place next object on scale', 'success');
        } else if (netWeight > SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE) {
            // Still has weight on scale
            this.uiController.updateGuidance('Measurement Done - Put the next object on', 'warning');
        }
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
    
    checkWeightStability(stableWeightReadings, threshold = 0.4, minReadings = 2) {
        // Check if we have enough readings
        if (!stableWeightReadings || stableWeightReadings.length < minReadings) {
            console.log('Weight Stability: Not enough readings yet.');
            return false;
        }

        // Get the last n readings
        const recentReadings = stableWeightReadings.slice(-minReadings);
        
        // NEW: Check if readings are near zero (noise)
        const isNearZero = recentReadings.every(weight => 
            Math.abs(weight) <= SCALE_CONSTANTS.WEIGHT_THRESHOLDS.ZERO_TOLERANCE
        );
        
        if (isNearZero) {
            console.log('Weight Stability: Readings near zero - considering noise');
            return false;
        }
        
        // Calculate max and min from recent readings
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
            isNearZero: isNearZero  // Added to logging
        });

        return isStable;
    }
}
