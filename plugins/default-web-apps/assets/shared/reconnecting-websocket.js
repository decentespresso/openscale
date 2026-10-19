(function (global) {
    function ReconnectingWebSocket(url, protocols, options) {
        if (protocols && typeof protocols === 'object' && !Array.isArray(protocols)) {
            options = protocols;
            protocols = undefined;
        }

        this.url = url;
        this.protocols = protocols;
        this.reconnectInterval = options?.reconnectInterval || 1000;
        this.maxReconnectInterval = options?.maxReconnectInterval || 30000;
        this.timeoutInterval = options?.timeoutInterval || 2000;
        this.debug = false;
        this.forcedClose = false;
        this.reconnectAttempts = 0;
        this.listeners = {};
        this.ws = null;
        this.open();
    }

    ReconnectingWebSocket.CONNECTING = WebSocket.CONNECTING;
    ReconnectingWebSocket.OPEN = WebSocket.OPEN;
    ReconnectingWebSocket.CLOSING = WebSocket.CLOSING;
    ReconnectingWebSocket.CLOSED = WebSocket.CLOSED;

    ReconnectingWebSocket.prototype.addEventListener = function (type, listener) {
        if (!this.listeners[type]) {
            this.listeners[type] = new Set();
        }
        this.listeners[type].add(listener);
    };

    ReconnectingWebSocket.prototype.removeEventListener = function (type, listener) {
        this.listeners[type]?.delete(listener);
    };

    ReconnectingWebSocket.prototype.dispatch = function (type, event) {
        if (typeof this[`on${type}`] === 'function') {
            this[`on${type}`](event);
        }
        this.listeners[type]?.forEach((listener) => listener.call(this, event));
    };

    ReconnectingWebSocket.prototype.open = function () {
        if (this.ws && this.ws.readyState <= WebSocket.OPEN) {
            return;
        }

        const ws = this.protocols
            ? new WebSocket(this.url, this.protocols)
            : new WebSocket(this.url);
        this.ws = ws;

        const timeout = setTimeout(() => {
            if (ws.readyState === WebSocket.CONNECTING) {
                ws.close();
            }
        }, this.timeoutInterval);

        ws.addEventListener('open', (event) => {
            clearTimeout(timeout);
            this.reconnectAttempts = 0;
            this.dispatch('open', event);
        });

        ws.addEventListener('message', (event) => {
            this.dispatch('message', event);
        });

        ws.addEventListener('error', (event) => {
            this.dispatch('error', event);
        });

        ws.addEventListener('close', (event) => {
            clearTimeout(timeout);
            this.dispatch('close', event);

            if (this.forcedClose) {
                return;
            }

            const delay = Math.min(
                this.reconnectInterval * Math.max(1, this.reconnectAttempts + 1),
                this.maxReconnectInterval
            );
            this.reconnectAttempts++;
            setTimeout(() => this.open(), delay);
        });
    };

    ReconnectingWebSocket.prototype.send = function (data) {
        if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
            throw new Error('WebSocket is not open');
        }
        this.ws.send(data);
    };

    ReconnectingWebSocket.prototype.close = function (code, reason) {
        this.forcedClose = true;
        this.ws?.close(code, reason);
    };

    ReconnectingWebSocket.prototype.refresh = function () {
        this.ws?.close();
    };

    Object.defineProperty(ReconnectingWebSocket.prototype, 'readyState', {
        get() {
            return this.ws ? this.ws.readyState : WebSocket.CLOSED;
        }
    });

    Object.defineProperty(ReconnectingWebSocket.prototype, 'bufferedAmount', {
        get() {
            return this.ws ? this.ws.bufferedAmount : 0;
        }
    });

    global.ReconnectingWebSocket = ReconnectingWebSocket;
})(window);
