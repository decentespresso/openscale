import { DebugLogger as Debug } from '../debug-logger.js';

export class BaseConnection {
    constructor(uiController) {
        this.ui = uiController;
        this.isConnected = false;
        this.notificationHandler = null;
        this.heartbeatInterval = null;
    }

    setNotificationHandler(handler) {
        this.notificationHandler = handler;
    }
//NOTE: heartbeat is older android version to stay connected with Decent scale. See programmer's guide for more details. 
    // startHeartbeat() {
    //     Debug.log('HEARTBEAT', 'Starting heartbeat');
    //     if (this.heartbeatInterval) {
    //         clearInterval(this.heartbeatInterval);
    //     }

    //     // Send initial heartbeat immediately
    //     this.sendHeartbeat();

    //     // Then set up interval
    //     this.heartbeatInterval = setInterval(() => {
    //         this.sendHeartbeat();
    //     }, 4000); // 4 seconds interval
    // }

    // async sendHeartbeat() {
    //     try {
    //         await this.sendCommand(new Uint8Array([0x03, 0x0a, 0x03, 0xff, 0xff, 0x00, 0x0a]));
    //         Debug.log('HEARTBEAT', 'Heartbeat sent successfully');
    //     } catch (error) {
    //         Debug.log('HEARTBEAT_ERROR', 'Failed to send heartbeat:', error);
    //         // If heartbeat fails, attempt to stop it
    //         this.stopHeartbeat();
    //     }
    // }

    // stopHeartbeat() {
    //     Debug.log('HEARTBEAT', 'Stopping heartbeat');
    //     if (this.heartbeatInterval) {
    //         clearInterval(this.heartbeatInterval);
    //         this.heartbeatInterval = null;
    //     }
    // }

    async connect() { throw new Error('Must implement connect()'); }
    async disconnect() { throw new Error('Must implement disconnect()'); }
    async sendCommand(data) { throw new Error('Must implement sendCommand()'); }
}
