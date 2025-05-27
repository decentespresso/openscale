import { BaseConnection } from './base-connection.js';
import { SCALE_CONSTANTS } from '../constants.js';
import { DebugLogger as Debug } from '../debug-logger.js';

export class BluetoothConnection extends BaseConnection {
    constructor(uiController) {
        super(uiController);
        this.device = null;
        this.server = null;
        this.readCharacteristic = null;
        this.writeCharacteristic = null;
    }

    async connect() {
        try {
            this.ui.updateStatus('Scanning...');
            this.device = await this._findAddress();
            
            if (!this.device) {
                throw new Error('Decent Scale not found');
            }

            this.ui.updateStatus('Connecting...');
            await this._connect();
            
            // Make sure we set status AFTER successful connection
            this.isConnected = true;
            // this.startHeartbeat();
            // Debug.log('BLE', 'Started heartbeat');
            this.ui.updateStatus('Connected to Decent Scale');  // Updated status message
            this.ui.updateConnectButton('Disconnect');
            Debug.log('Connectbutton', 'ButtonUI update to Disconnect');
            return true;
        } catch (error) {
            Debug.log('BLE_ERROR', 'Connection error:', error);
            
            // Handle user cancellation specifically
            if (error.name === 'NotFoundError') {
                this.ui.updateStatus('Connection cancelled');
            } else {
                this.ui.updateStatus(`Connection failed: ${error.message}`);
            }
            
            this.isConnected = false;
            return false;
        }
    }

    async _findAddress() {
        const device = await navigator.bluetooth.requestDevice({
            filters: [{ name: 'Decent Scale' }],
            optionalServices: [SCALE_CONSTANTS.CHAR_READ]
        });
        return device;
    }

    async _connect() {
        try {
            this.server = await this.device.gatt.connect();
            const service = await this.server.getPrimaryService(SCALE_CONSTANTS.CHAR_READ);
            
            this.readCharacteristic = await service.getCharacteristic(SCALE_CONSTANTS.READ_CHARACTERISTIC);
            this.writeCharacteristic = await service.getCharacteristic(SCALE_CONSTANTS.WRITE_CHARACTERISTIC);
            
            await this._enableNotification();
            Debug.log('BLE', 'Connected and characteristics initialized');
            return true;
        } catch (error) {
            Debug.log('BLE_ERROR', '_connect failed:', error);
            throw error;  // Propagate error to main connect method
        }
    }

    async _enableNotification() {
        await this.readCharacteristic.startNotifications();
        // Use arrow function to preserve 'this' context and handle events properly
        this.readCharacteristic.addEventListener('characteristicvaluechanged', 
            (event) => {
                if (typeof this.notificationHandler === 'function') {
                    this.notificationHandler(event);
                } else {
                    Debug.log('BLE_ERROR', 'Notification handler not set or invalid');
                }
            }
        );
        Debug.log('BLE', 'Notifications enabled');
    }
    async _disable_notification() {
        if (this.readCharacteristic) {
            await this.readCharacteristic.stopNotifications();
            // this.readCharacteristic.removeEventListener('characteristicvaluechanged', this.notification_handler.bind(this));
        }
    }
    async disconnect() {
        // this.stopHeartbeat();
        await this._disconnect();

        this.isConnected = false;
        this.ui.updateStatus('Bluetooth Disconnected');
        this.ui.updateConnectButton('Connect to Scale'); // Reset button to Connect
    }
    async _disconnect() {
        if (this.server) {
            try {
                if (this.readCharacteristic) {
                    try {
                        await this._disable_notification();
                    } catch (err) {
                        Debug.log('BLE_WARN', 'Failed to stop notifications (may already be disconnected):', err);
                    }
                }
                await this.server.disconnect();
            } catch (err) {
                Debug.log('BLE_WARN', 'Error during server disconnect:', err);
            }
        }
        this.device = null;
        this.server = null;
        this.readCharacteristic = null;
        this.writeCharacteristic = null;
        if (this.ui) this.ui.updateStatus('Not connected');
    }
    async sendCommand(data) {
        if (!this.isConnected) throw new Error('Device not connected');
        await this.writeCharacteristic.writeValue(new Uint8Array(data));
    }
}
