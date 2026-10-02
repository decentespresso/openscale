from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CHECK = r'''
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const assets = process.argv[1];
const html = fs.readFileSync(path.join(assets, 'index.html'), 'utf8');
assert(html.includes('<script src="shared/reconnecting-websocket.js"></script>'));
const start = html.indexOf('    const ws = ');
const end = html.indexOf("    document.getElementById('reset-wifi-button')", start);
const timers = new Map();
const sockets = [];
const elements = new Map();
let timerId = 0;
let polls = 0;
class Socket {
  static CONNECTING = 0; static OPEN = 1; static CLOSING = 2; static CLOSED = 3;
  constructor() { this.readyState = 0; this.events = {}; this.sent = []; sockets.push(this); }
  addEventListener(name, listener) { this.events[name] = listener; }
  emit(name, event = {}) { this.events[name](event); }
  send(value) { assert.equal(this.readyState, 1); this.sent.push(value); }
  close() { this.readyState = 3; this.emit('close'); }
}
const context = vm.createContext({
  WebSocket: Socket, alert() {},
  document: { getElementById(id) {
    if (!elements.has(id)) elements.set(id, {style: {}, addEventListener() {}});
    return elements.get(id);
  } },
  setTimeout(callback, delay) { const id = ++timerId; timers.set(id, {callback, delay}); return id; },
  clearTimeout(id) { timers.delete(id); },
  setInterval() { ++polls; }
});
context.window = context;
context.location = {host: 'scale.test'};
vm.runInContext(fs.readFileSync(path.join(assets, 'shared/reconnecting-websocket.js'), 'utf8'), context);
vm.runInContext(html.slice(start, end), context);
for (let cycle = 0; cycle < 3; ++cycle) {
  const socket = sockets[cycle];
  socket.readyState = 1;
  socket.emit('open');
  assert.deepEqual(socket.sent, ['status']);
  assert.equal(elements.get('weight-value').style.color, '');
  socket.emit('message', {data: '{"grams":12.3}'});
  assert.equal(elements.get('weight-value').textContent, '12.3 g');
  socket.close();
  assert.equal(elements.get('connection-status').textContent, 'Disconnected');
  const pending = [...timers.entries()];
  assert.equal(pending.length, 1);
  const [id, timer] = pending[0];
  assert.equal(timer.delay, 1000);
  timers.delete(id);
  timer.callback();
  assert.equal(sockets.length, cycle + 2);
}
assert.equal(polls, 1);
console.log('Dashboard reconnects, requests status, restores readings, and keeps one polling loop');
'''


if __name__ == "__main__":
    subprocess.run(["node", "-e", CHECK,
                    str(ROOT / "plugins/default-web-apps/assets")], check=True)
