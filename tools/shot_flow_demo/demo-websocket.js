(function () {
    const NativeWebSocket = window.WebSocket;

    class DemoWebSocket extends EventTarget {
        static CONNECTING = NativeWebSocket.CONNECTING;
        static OPEN = NativeWebSocket.OPEN;
        static CLOSING = NativeWebSocket.CLOSING;
        static CLOSED = NativeWebSocket.CLOSED;

        constructor() {
            super();
            this.readyState = DemoWebSocket.CONNECTING;
            this.bufferedAmount = 0;
            this.timer = null;
            queueMicrotask(() => {
                this.readyState = DemoWebSocket.OPEN;
                this.dispatchEvent(new Event('open'));
            });
        }

        send(message) {
            if (this.timer || !String(message).includes('rate_hz')) return;
            let milliseconds = 0;
            let weight = 0;
            this.timer = setInterval(() => {
                milliseconds += 100;
                const seconds = milliseconds / 1000;
                const flow = seconds < 2 ? seconds * 0.9 :
                    seconds < 18 ? 1.8 + Math.sin(seconds * 0.7) * 0.12 :
                    seconds < 25 ? Math.max(0, 1.8 * (25 - seconds) / 7) : 0;
                weight += flow / 10;
                this.dispatchEvent(new MessageEvent('message', {
                    data: JSON.stringify({ grams: weight, ms: milliseconds })
                }));
                if (milliseconds >= 30000) {
                    clearInterval(this.timer);
                    this.timer = null;
                }
            }, 100);
        }

        close() {
            clearInterval(this.timer);
            this.readyState = DemoWebSocket.CLOSED;
            this.dispatchEvent(new CloseEvent('close'));
        }
    }

    window.WebSocket = DemoWebSocket;
})();