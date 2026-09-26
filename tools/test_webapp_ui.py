import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "plugins/default-web-apps/assets"

NODE_CHECK = r"""
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const assets = process.argv[1];
const load = async file => import('data:text/javascript;base64,' +
  fs.readFileSync(path.join(assets, file)).toString('base64'));

function element() {
  const classes = new Set();
  return {
    textContent: '', value: '1', style: {}, listeners: {}, children: [],
    disabled: true,
    classList: {
      add(...names) { names.forEach(name => classes.add(name)); },
      remove(...names) { names.forEach(name => classes.delete(name)); },
      toggle(name, force) { if (force) classes.add(name); else classes.delete(name); },
      contains(name) { return classes.has(name); }
    },
    addEventListener(name, callback) { this.listeners[name] = callback; },
    appendChild(child) { this.children.push(child); },
    setAttribute(name) { if (name === 'disabled') this.disabled = true; },
    removeAttribute(name) { if (name === 'disabled') this.disabled = false; }
  };
}

(async () => {
  const html = fs.readFileSync(path.join(assets, 'Weigh_Save/weigh_save.html'), 'utf8');
  const elements = Object.fromEntries([...html.matchAll(/id="([^"]+)"/g)]
    .map(([, id]) => [id, element()]));
  const listeners = {};
  global.document = {
    getElementById: id => elements[id] || null,
    createElement: element,
    fullscreenEnabled: true,
    addEventListener(name, callback) { listeners[name] = callback; }
  };
  const intervals = new Map();
  Date.now = () => 10000;
  global.setInterval = callback => { const id = Symbol(); intervals.set(id, callback); return id; };
  global.clearInterval = id => intervals.delete(id);
  global.setTimeout = () => 1;
  global.alert = message => { throw Error(message); };
  const {UIController} = await load('Weigh_Save/modules/ui-controller.js');
  const {TimerManager} = await load('Weigh_Save/modules/timer.js');
  const timer = new TimerManager();
  const ui = new UIController(timer);
  timer.setUIController(ui);
  ui.updateWeightDisplay(12.3);
  ui.updateRateDisplay(2.4);
  assert.equal(elements.weight.textContent, 'Weight: 12.3 g');
  assert.equal(elements.rate.textContent, 'Rate: 2.4 g/s');
  ui.toggleTimer();
  assert.equal(elements.toggleTimer.textContent, 'Stop');
  assert.equal(timer.measurementInterval, 1000);
  assert.equal(timer.shouldTakeMeasurement(), true);
  assert.equal(timer.shouldTakeMeasurement(), false);
  [...intervals.values()][0]();
  assert.equal(elements.timer.textContent, 'Timer: 1');
  ui.displayWeightReadings(['1, 12.3g, 2.4g/s, 2026-09-26']);
  assert.equal(elements.weightReadings.children.length, 1);
  assert.equal(elements.exportCSV.disabled, false);
  assert.equal(elements.exportJSON.disabled, false);
  ui.displayWeightReadings([]);
  assert.equal(elements.exportCSV.disabled, true);
  ui.toggleTimer();
  assert.equal(intervals.size, 0);
  assert.equal(elements.toggleTimer.textContent, 'Start');
  assert.equal(elements.timer.textContent, 'Timer: 0s');
  assert.equal(elements.measurementAlert.style.display, 'block');
  assert.equal(typeof elements['fullscreen-button'].listeners.click, 'function');
  assert.equal(typeof listeners.fullscreenchange, 'function');
  console.log('webapp UI checks passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
"""


if __name__ == "__main__":
    subprocess.run(["node", "-e", NODE_CHECK, str(ASSETS)], cwd=ROOT, check=True)
