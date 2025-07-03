const { WebSocketServer } = require('ws');
const noble = require('@abandonware/noble');

// --- UUID Constants

const SCALE_NAME = 'Decent Scale';
const PORT = 8080;
const SERVICE_UUID = 'fff0';
const WEIGHT_CHAR_UUID = '0000fff400001000800000805f9b34fb';   // read/notify
const COMMAND_CHAR_UUID = '000036f500001000800000805f9b34fb';  // write

// WebSocket Server ---
const wss = new WebSocketServer({ port: PORT });
console.log(`WebSocket bridge server started on ws://localhost:${PORT}`);

let scalePeripheral = null;
let weightCharacteristic = null;
let commandCharacteristic = null;
let clientSocket = null;

// --- Handle WebSocket Client Connections ---
wss.on('connection', (ws) => {
    console.log('Web client connected.');
    clientSocket = ws;

    if (noble.state === 'poweredOn') {
        startScanning();
    } else {
        noble.once('stateChange', startScanning);
    }

    ws.on('message', (message) => {
        try {
            const data = JSON.parse(message);
            console.log('Received command from client:', data.command);

            if (data.command === 'tare' && commandCharacteristic) {
                const tareCommand = Buffer.from([0x03, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x0C]);
                commandCharacteristic.write(tareCommand, false, (error) => {
                    if (error) {
                        console.error('Tare command failed:', error);
                    } else {
                        console.log('Tare command sent successfully.');
                    }
                });
            }
        } catch (e) {
            console.error('Failed to parse message from client:', e);
        }
    });

    ws.on('close', () => {
        console.log('Web client disconnected.');
        clientSocket = null;
        if (scalePeripheral && scalePeripheral.state === 'connected') {
            console.log('Disconnecting from scale...');
            scalePeripheral.disconnect();
        }
        noble.stopScanning();
    });
});

// --- Bluetooth (Noble) Logic ---

function startScanning() {
    console.log('Bluetooth powered on. Starting scan for', SCALE_NAME);
    noble.startScanning([], false);
}

noble.on('discover', (peripheral) => {
    if (peripheral.advertisement.localName === SCALE_NAME) {
        noble.stopScanning();
        console.log(`Found scale: ${peripheral.id}`);
        scalePeripheral = peripheral;

        peripheral.once('disconnect', () => {
            console.log('Scale disconnected.');
            sendMessageToClient({ type: 'status', message: 'Scale Disconnected' });
            scalePeripheral = null;
            weightCharacteristic = null;
            commandCharacteristic = null;
            if (clientSocket) {
                startScanning();
            }
        });

        connectToScale(peripheral);
    }
});

async function connectToScale(peripheral) {
    try {
        console.log('Connecting to scale...');
        sendMessageToClient({ type: 'status', message: 'Connecting...' });
        await peripheral.connectAsync();
        console.log('Connected. Discovering services and characteristics...');
        sendMessageToClient({ type: 'status', message: 'Discovering...' });

        // Discover the service
        const services = await peripheral.discoverServicesAsync([SERVICE_UUID]);
        if (!services || services.length === 0) {
            console.error(`Could not find the required service '${SERVICE_UUID}'.`);
            peripheral.disconnect();
            return;
        }
        const scaleService = services[0];
        console.log(`Found service '${SERVICE_UUID}'. Now discovering characteristics...`);

        // Discover all characteristics in the service
        const characteristics = await scaleService.discoverCharacteristicsAsync([]);
        console.log('--- Discovered Characteristics ---');
        characteristics.forEach(char => {
            console.log(`  Characteristic UUID: ${char.uuid}, Properties: ${char.properties.join(', ')}`);
        });
        console.log('---------------------------------');

        // Find characteristics by full UUID
        weightCharacteristic = characteristics.find(c => c.uuid === WEIGHT_CHAR_UUID);
        commandCharacteristic = characteristics.find(c => c.uuid === COMMAND_CHAR_UUID);

        if (weightCharacteristic && commandCharacteristic) {
            console.log('Found required characteristics. Subscribing to weight updates.');
            sendMessageToClient({ type: 'status', message: 'Connected' });

            weightCharacteristic.on('data', (data, isNotification) => {
                if (data.readUInt8(0) === 0x03 && data.length === 7) {
                    const type = data.readUInt8(1);
                    if (type === 0xCE || type === 0xCA) {
                        const weight = data.readInt16BE(2) / 10;
                        sendMessageToClient({ type: 'weight', value: weight });
                    }
                }
            });

            await weightCharacteristic.subscribeAsync();
            console.log('Subscribed to weight notifications.');
        } else {
            console.error('Could not find required characteristics.');
            peripheral.disconnect();
        }
    } catch (error) {
        console.error('Failed to connect or setup scale:', error);
        if (peripheral.state === 'connected') {
            peripheral.disconnect();
        }
    }
}

// --- Helper to send messages to the connected web client ---
function sendMessageToClient(message) {
    if (clientSocket && clientSocket.readyState === clientSocket.OPEN) {
        clientSocket.send(JSON.stringify(message));
    }
}
