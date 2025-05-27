export class DebugLogger {
    static DEBUG = true;

    static log(category, message, data = null) {
        if (!this.DEBUG) return;

        const timestamp = new Date().toISOString().split('T')[1].slice(0, -1);
        const logMessage = `[${timestamp}] [${category}] ${message}`;
        
        if (data) {
            console.log(logMessage, data);
        } else {
            console.log(logMessage);
        }
    }

    static hexView(data) {
        if (!this.DEBUG || !data) return;
        
        if (data instanceof Uint8Array) {
            return Array.from(data).map(b => '0x' + b.toString(16).padStart(2, '0')).join(' ');
        }
        return 'Not a Uint8Array';
    }
}
