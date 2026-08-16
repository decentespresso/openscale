export const SCALE_CONSTANTS = {
    CHAR_READ: '0000fff0-0000-1000-8000-00805f9b34fb',
    CHAR_WRITE: '000036f5-0000-1000-8000-00805f9b34fb',
    READ_CHARACTERISTIC: '0000fff4-0000-1000-8000-00805f9b34fb',
    WRITE_CHARACTERISTIC: '000036f5-0000-1000-8000-00805f9b34fb',

    FSM_STATES: {
        MEASURING: 'measuring',
        REMOVAL_PENDING: 'removal_pending',
        WAITING_FOR_NEXT: 'waiting_for_next',
        CONTAINER_REMOVED: 'container_removed'
    },
    WEIGHT_THRESHOLDS: {
        MINIMUM: -0.4,          // Threshold for container removal detection
        CONTAINER_TOLERANCE: 0.6, // Increased tolerance for container replacement
        ZERO_TOLERANCE: 0.2,     // Tolerance for considering weight as "zero"
        NEW_CONTAINER_TOLERANCE: 1
    }
};
