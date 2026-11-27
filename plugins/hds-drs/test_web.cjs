const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');
const { runInNewContext } = require('node:vm');

const readAsset = name => readFileSync(join(__dirname, 'assets', name), 'utf8');
const html = readAsset('index.html');
const elements = Object.fromEntries(Array.from(html.matchAll(/id="([^"]+)"/g),
  ([, id]) => [id, { classList: { toggle() {} } }]));
const buttons = Array.from(html.matchAll(/data-command="([^"]+)"/g), ([, command]) => ({
  dataset: { command },
  addEventListener(type, handler) { this[type] = handler; },
}));

let socket;
class WebSocket {
  static OPEN = 1;
  readyState = WebSocket.OPEN;
  handlers = {};
  sent = [];
  constructor(url) { this.url = url; socket = this; }
  addEventListener(type, handler) { this.handlers[type] = handler; }
  send(command) { this.sent.push(command); }
}

runInNewContext(readAsset('app.js'), {
  document: { getElementById(id) { return elements[id]; }, querySelectorAll() { return buttons; } },
  location: { protocol: 'http:', host: 'hds.local' },
  WebSocket,
  setTimeout() {},
});

const frame = value => socket.handlers.message({ data: JSON.stringify(value) });
assert.equal(socket.url, 'ws://hds.local/snapshot');
socket.handlers.open();
frame({ type: 'status', drs_enabled: false, grams: 10, drs_weight: 99 });
assert.ok(buttons.every(button => button.disabled));
assert.equal(elements.extraction.textContent, 'DRS unavailable');
assert.equal(elements.weight.innerHTML, '10.0<span class="unit">g</span>');
buttons.forEach(button => button.click());
assert.deepEqual(socket.sent, ['status']);

frame({ type: 'status', drs_enabled: true, drs_weight: 38, grams: 32 });
const timerButtons = buttons.filter(button => button.dataset.command.startsWith('drs timer '));
assert.ok(buttons.every(button => button.disabled === timerButtons.includes(button)));
timerButtons.forEach(button => button.click());
assert.deepEqual(socket.sent, ['status']);
assert.equal(elements.weight.innerHTML, '38.0<span class="unit">g</span>');
frame({ grams: 30, drs: { enabled: 1, weight: 32.1, extract: 1, mode: 1, flow: 2.3, ratio: 1.8, dose: 18, timer: 12, trun: 1 } });
assert.equal(elements.weight.innerHTML, '32.1<span class="unit">g</span>');
assert.equal(elements.timer.textContent, '12s');
assert.equal(elements.extraction.textContent, 'Extraction on');
buttons.forEach(button => button.click());
assert.deepEqual(socket.sent.slice(1), ['drs pour', 'drs espresso', 'drs toggle', 'drs tare', 'drs timer start', 'drs timer stop', 'drs timer zero']);

socket.handlers.close();
assert.ok(buttons.every(button => button.disabled));
process.stdout.write('SofronioDRS web checks passed\n');
