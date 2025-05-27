import { BaseConnection } from './base-connection.js';
import { DebugLogger as Debug } from '../debug-logger.js';
//This is the module to make use of WEB USB api to establish connection with half decent scale, and pass data to other modules similar to BLE connection.
export class USBConnection extends BaseConnection {
    constructor(uiController) {
        super(uiController);
        this.device = null;
        this.interfaceNumber = 0;
        this.pollInterval = null;
        this.endpointIn = null; // Only use IN endpoint for both in/out
        this.VENDOR_ID = 0x1a86;    // CH340 vendor ID
        this.PRODUCT_ID = 0x7523;   // CH340 product ID
        this.BAUD_RATE = 115200;    // ESP32-S3 baud rate
        this.dataBuffer = new Uint8Array(0);
        this._checkUSBSupport();
    }

    _checkUSBSupport() {
        if (!navigator.usb) {
            console.error('WebUSB not available');
            return false;
        }
        console.log('WebUSB is available');
        return true;
    }

    async _initializeEndpoints() {
        const interfaceObj = this.device.configuration.interfaces[0];
        const alternate = interfaceObj.alternate;
        Debug.log('USB', 'Available endpoints:', alternate.endpoints);

        // Find the first IN endpoint (just like your HTML)
        const inEp = alternate.endpoints.find(e => e.direction === 'in');
        if (!inEp) throw new Error('No IN endpoint found');
        this.endpointIn = inEp.endpointNumber;
        Debug.log('USB', `Using IN endpoint: 0x${this.endpointIn.toString(16)}`);
    }

    async _initializeDevice() {
        try {
            await this.device.open();
            Debug.log('USB', 'Device opened');

            if (this.device.configuration === null) {
                await this.device.selectConfiguration(1);
            }

            this.interfaceNumber = this.device.configuration.interfaces[0].interfaceNumber;
            Debug.log('USB', `Claiming interface ${this.interfaceNumber}`);

            await this.device.claimInterface(this.interfaceNumber);

            await this._initCH340();
            Debug.log('USB', 'CH340 Initialized');

            await this._initializeEndpoints();

            // --- Send command to enable weight output (use IN endpoint, like HTML) ---
            const enableWeightCmd = new Uint8Array([0x03, 0x20, 0x01]);
            await this.device.transferOut(this.endpointIn, enableWeightCmd);
            Debug.log('USB', 'Sent enable weight command:', enableWeightCmd);

            this.startPolling();
            Debug.log('USB', 'Device initialized successfully');
            return true;
        } catch (error) {
            Debug.log('USB_ERROR', 'Device initialization failed:', error);
            if (this.device && this.interfaceNumber != null) {
                try { await this.device.releaseInterface(this.interfaceNumber); } catch(e) {}
            }
            if (this.device && this.device.opened) {
                try { await this.device.close(); } catch(e) {}
            }
            this.device = null;
            return false;
        }
    }

    async connect() {
        try {
            if (!this._checkUSBSupport()) {
                throw new Error('WebUSB not supported');
            }

            Debug.log('USB', 'Requesting USB device...');
            try {
                this.device = await navigator.usb.requestDevice({
                    filters: []
                });
            } catch (error) {
                if (error.name === 'NotFoundError') {
                    Debug.log('USB', 'Device selection cancelled');
                    this.ui.updateStatus('Connection cancelled');
                    return false;
                }
                throw error;
            }

            const initialized = await this._initializeDevice();
            if (!initialized) {
                throw new Error('Failed to initialize device');
            }

            this.isConnected = true;
            this.ui.updateStatus('USB Connected');
            this.ui.updateConnectButton('Disconnect');
            //this.startHeartbeat();
            return true;

        } catch (error) {
            Debug.log('USB_ERROR', 'Connection failed:', error);
            this.ui.updateStatus(`USB Connection failed: ${error.message}`);
            return false;
        }
    }

    async _initCH340() {
        // CH340 specific initialization sequence
        await this.device.controlTransferOut({
            requestType: 'vendor',
            recipient: 'device',
            request: 0x9A,
            value: 0x2518,
            index: 0x0000
        });

        // Set baud rate
        const encodedBaud = this._encodeCH340BaudRate(this.BAUD_RATE);
        await this.device.controlTransferOut({
            requestType: 'vendor',
            recipient: 'device',
            request: 0x9A,
            value: 0x2518,
            index: encodedBaud
        });

        // Set line control
        await this.device.controlTransferOut({
            requestType: 'vendor',
            recipient: 'device',
            request: 0x9A,
            value: 0x2518,
            index: 0x00C3
        });
    }

    _encodeCH340BaudRate(baudRate) {
        const CH340_BAUDBASE_FACTOR = 1532620800;
        const divisor = Math.floor(CH340_BAUDBASE_FACTOR / baudRate);
        return ((divisor & 0xFF00) | 0x00C0);
    }

    startPolling() {
        Debug.log('USB', 'Starting USB polling');
        let errorCount = 0;
        this.dataBuffer = new Uint8Array(0);

        if (this.pollInterval) {
            clearInterval(this.pollInterval);
            this.pollInterval = null;
        }

        this.pollInterval = setInterval(async () => {
            if (!this.device || !this.endpointIn || !this.device.opened) {
                Debug.log('USB_WARN', 'Polling attempted but device is not ready or open.');
                this.stopPolling();
                return;
            }

            try {
                const result = await this.device.transferIn(this.endpointIn, 64);
                if (result.status === 'ok' && result.data && result.data.byteLength > 0) {
                    const newData = new Uint8Array(result.data.buffer);
                    this.dataBuffer = this._appendBuffer(this.dataBuffer, newData);
                    this._processBuffer();
                    errorCount = 0;
                } else if (result.status !== 'ok') {
                    Debug.log('USB_WARN', `USB transferIn status: ${result.status}`);
                    if(result.status === 'stall') {
                        Debug.log('USB', 'Attempting to clear IN endpoint stall...');
                        try {
                            await this.device.clearHalt('in', this.endpointIn);
                            Debug.log('USB', 'Cleared IN endpoint stall successfully.');
                        } catch (clearError) {
                            Debug.log('USB_ERROR', 'Failed to clear IN endpoint stall:', clearError);
                            errorCount++;
                        }
                    } else {
                        errorCount++;
                    }
                }
            } catch (error) {
                errorCount++;
                Debug.log('USB_ERROR', `Polling error (Count: ${errorCount}):`, error);
                if (error.message.includes("disconnected") || error.name === 'NetworkError' || error.message.includes("device unavailable") || error.message.includes("No device selected")) {
                    Debug.log('USB_ERROR', 'Device disconnected detected during polling.');
                    this.stopPolling();
                    this.isConnected = false;
                    this.device = null;
                    if (this.ui) {
                        this.ui.updateStatus('USB Disconnected (Polling Error)');
                        this.ui.updateConnectButton('Connect to Scale');
                    }
                    return;
                } else if (errorCount > 5) {
                    Debug.log('USB_ERROR', `Too many consecutive polling errors (${errorCount}), attempting disconnect...`);
                    this.stopPolling();
                    await this.disconnect();
                    return;
                }
            }
        }, 100);
    }

    stopPolling() {
        if (this.pollInterval) {
            Debug.log('USB', 'Stopping USB polling');
            clearInterval(this.pollInterval);
            this.pollInterval = null;
        }
    }

    _appendBuffer(buffer1, buffer2) {
        let tmp = new Uint8Array(buffer1.length + buffer2.length);
        tmp.set(buffer1, 0);
        tmp.set(buffer2, buffer1.length);
        return tmp;
    }

    // Process 7-byte packets starting with 0x03 0xCE or 0x03 0xCA
    _processBuffer() {
        if (this.dataBuffer.length > 0) {
            Debug.log('USB_DATA', 'Raw buffer:', Array.from(this.dataBuffer).map(b => '0x' + b.toString(16)).join(' '));
        }
        while (this.dataBuffer.length >= 7) {
            if (
                this.dataBuffer[0] === 0x03 &&
                (this.dataBuffer[1] === 0xCE || this.dataBuffer[1] === 0xCA)
            ) {
                const packet = this.dataBuffer.slice(0, 7);
                Debug.log('USB_DATA', '7-byte packet:', packet);

                // Notify as DataView for compatibility
                if (this.notificationHandler) {
                    const dataView = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
                    this.notificationHandler({
                        target: {
                            value: dataView
                        }
                    });
                }

                // Remove processed packet
                this.dataBuffer = this.dataBuffer.slice(7);
            } else {
                // Skip one byte and try again (resync)
                Debug.log('USB_DATA', 'Skipping byte (not a valid packet start):', this.dataBuffer[0]);
                this.dataBuffer = this.dataBuffer.slice(1);
            }
        }
    }

    async disconnect() {
        //this.stopHeartbeat();
        this.stopPolling();
        if (this.device) {
            try {
                await this.device.releaseInterface(this.interfaceNumber);
                await this.device.close();
                this.device = null;
                this.isConnected = false;
                this.ui.updateStatus('USB Disconnected');
                this.ui.updateConnectButton('Connect to Scale');
                Debug.log('USB', 'Device disconnected');
            } catch (error) {
                Debug.log('USB_ERROR', 'Disconnect error:', error);
                console.error('USB Disconnect error:', error);
                this.ui.updateStatus(`USB Disconnect failed: ${error.message}`);
            }
        }
    }

    // Send command using IN endpoint (like your HTML)
    async sendCommand(data) {
        if (!this.device || !this.endpointIn) {
            throw new Error('Device not properly initialized');
        }
        try {
            Debug.log('USB_CMD', `Sending command to endpoint 0x${this.endpointIn.toString(16)}:`, Debug.hexView(data));
            const result = await this.device.transferOut(this.endpointIn, data);
            Debug.log('USB_CMD', 'Command sent:', result.status);
            return result;
        } catch (error) {
            Debug.log('USB_ERROR', 'Send command error:', error);
            console.error('USB send command error:', error);
        }
    }
}
