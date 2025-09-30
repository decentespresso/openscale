export const SCALE_CONSTANTS = {
    SERVICE_UUID: 0xfff0,
    READ_CHARACTERISTIC: 0xfff4,
    WRITE_CHARACTERISTIC: 0x36f5,

    FSM_STATES: {
        MEASURING: 'measuring',
        REMOVAL_PENDING: 'removal_pending',
        WAITING_FOR_NEXT: 'waiting_for_next',
        CONTAINER_REMOVED: 'container_removed'
    },
    WEIGHT_THRESHOLDS: {
        MINIMUM: -0.4,          // Threshold for container removal detection
        CONTAINER_TOLERANCE: 0.6, // Increased tolerance for container replacement
        ZERO_TOLERANCE: 0.2     // Tolerance for considering weight as "zero"
    }
};