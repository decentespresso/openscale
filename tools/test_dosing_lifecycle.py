from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CHECK = r'''
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
let now = 0;
const timers = new Map();
let timerId = 0;
const uiState = {};
const ui = new Proxy({
  getWeightDisplay() { return uiState.weight; },
  updateWeightDisplay(weight) { uiState.weight = weight; },
  updateGuidance(message) { uiState.guidance = message; },
  updateDosingButton(label) { uiState.button = label; },
  enableSetContainerWeightButton() { uiState.containerEnabled = true; },
  disableSetContainerWeightButton() { uiState.containerEnabled = false; },
  showSetContainerWeightButton() { uiState.containerVisible = true; },
  hideSetContainerWeightButton() { uiState.containerVisible = false; }
}, {get(target, key) { return target[key] ?? (() => {}); }});
const inputs = {targetWeight: {value: '10'}, lowThreshold: {value: '9'}, highThreshold: {value: '11'}};
const warnings = [];
const context = vm.createContext({
  Date: class extends Date { static now() { return now; } },
  console: {log() {}, warn() {}, error() {}},
  setTimeout(callback) { const id = ++timerId; timers.set(id, callback); return id; },
  clearTimeout(id) { timers.delete(id); },
  WebSocket: {OPEN: 1},
  document: {getElementById(id) { return inputs[id]; }},
  alert(message) { warnings.push(message); }
});
const modules = new Map();
async function load(file) {
  if (modules.has(file)) return modules.get(file);
  const module = new vm.SourceTextModule(fs.readFileSync(file, 'utf8'), {context, identifier: file});
  modules.set(file, module);
  await module.link((specifier, parent) => load(path.resolve(path.dirname(parent.identifier), specifier)));
  await module.evaluate();
  return module;
}
(async () => {
  const app = process.argv[1];
  const {StateMachine} = (await load(path.join(app, 'modules/state-machine.js'))).namespace;
  const {DecentScale} = (await load(path.join(app, 'modules/scale.js'))).namespace;
  function scale() {
    now = 0;
    const result = new DecentScale(ui, new StateMachine(ui));
    result.dosingSettings = {targetWeight: 10, lowThreshold: 9, highThreshold: 11};
    result.dosingMode = true;
    result.soundEnabled = false;
    result.ws = {readyState: 1, sent: [], send(value) { this.sent.push(value); }};
    return result;
  }
  function sample(target, weight, advance = 500) {
    now += advance;
    target.handleWebSocketWeight(weight);
  }
  const ramp = scale();
  for (let index = 0; index < 20; ++index) sample(ramp, 1 + index * 0.2);
  assert.equal(ramp.weightData.length, 0);
  const settled = scale();
  for (let index = 0; index < 7; ++index) sample(settled, index === 6 ? 10.1 : 10);
  assert.equal(settled.weightData.length, 1);
  assert.equal(settled.weightData[0].weight, '10.1');
  sample(settled, 10.1);
  assert.equal(settled.weightData.length, 1);
  for (const event of ['stop', 'close', 'error']) {
    const stopped = scale();
    sample(stopped, 10);
    sample(stopped, 10);
    if (event === 'stop') stopped.toggleDosingMode();
    else {
      const main = fs.readFileSync(path.join(app, 'main.js'), 'utf8');
      const handler = main.split(`ws.addEventListener('${event}', () => {`)[1].split('});')[0];
      context.scale = stopped;
      context.ui = ui;
      vm.runInContext(handler, context);
    }
    now += 10000;
    for (const callback of timers.values()) callback();
    timers.clear();
    sample(stopped, 10);
    assert.equal(stopped.weightData.length, 0);
    assert.equal(stopped.dosingMode, false);
    stopped.startDosingAutomatically();
    sample(stopped, 10);
    sample(stopped, 10);
    assert.equal(stopped.weightData.length, 0);
  }
  const gap = scale();
  sample(gap, 10);
  sample(gap, 10);
  sample(gap, 10, 10000);
  assert.equal(gap.weightData.length, 0);
  for (const removedWeight of [0, -10]) {
    const removed = scale();
    for (let index = 0; index < 6; ++index) sample(removed, 10);
    sample(removed, removedWeight);
    now += 5000;
    for (const callback of timers.values()) callback();
    assert.equal(removed.weightData.length, 0);
    assert.equal(removed.stateMachine.stabilityWindow, null);
  }
  const cancelled = scale();
  cancelled.stopDosing();
  cancelled.toggleDosingMode();
  assert.equal(cancelled.waitingForContainerWeight, true);
  assert.equal(uiState.button, 'Stop');
  cancelled.toggleDosingMode();
  assert.equal(cancelled.waitingForContainerWeight, false);
  assert.equal(cancelled.dosingMode, false);
  assert.equal(uiState.button, 'Start');
  assert.equal(uiState.containerEnabled, false);
  assert.equal(uiState.containerVisible, false);
  for (let index = 0; index < 7; ++index) sample(cancelled, 10);
  assert.equal(cancelled.weightData.length, 0);
  assert.equal(cancelled.ws.sent.length, 0);
  for (const failure of ['closed', 'send']) {
    const disconnected = scale();
    disconnected.stopDosing();
    sample(disconnected, 100);
    disconnected.toggleDosingMode();
    if (failure === 'closed') disconnected.ws.readyState = 3;
    else disconnected.ws.send = () => { throw new Error('Connection lost'); };
    disconnected.setContainerWeight();
    assert.equal(disconnected.dosingMode, false);
    assert.equal(disconnected.waitingForContainerWeight, true);
    assert.equal(disconnected.tareWeight, 0);
    assert.equal(disconnected.containerWeight, 0);
    assert.equal(uiState.containerEnabled, true);
    assert.equal(warnings.at(-1), 'WebSocket not connected.');
    disconnected.ws = {readyState: 1, sent: [], send(value) { this.sent.push(value); }};
    for (let index = 0; index < 7; ++index) sample(disconnected, 110);
    assert.equal(disconnected.weightData.length, 0);
    disconnected.setContainerWeight();
    assert.equal(disconnected.dosingMode, true);
    assert.equal(disconnected.waitingForContainerWeight, false);
    assert.deepEqual(disconnected.ws.sent, ['tare']);
    assert.equal(uiState.containerEnabled, false);
    sample(disconnected, 0);
    for (let index = 0; index < 7; ++index) sample(disconnected, 10);
    assert.equal(disconnected.weightData.length, 1);
    assert.equal(disconnected.weightData[0].weight, '10.0');
  }
  const replacement = scale();
  sample(replacement, 10);
  sample(replacement, -100);
  sample(replacement, 2);
  assert.equal(uiState.guidance, 'New container detected, click zero to continue');
  assert.equal(replacement.stateMachine.currentState, 'container_removed');
  assert.equal(replacement.weightData.length, 0);
  replacement.tare();
  sample(replacement, 0);
  assert.equal(replacement.stateMachine.currentState, 'waiting_for_next');
  console.log('Dosing settling, cancellation, failed tare sends and replacement containers passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
'''


if __name__ == "__main__":
    subprocess.run(["node", "--experimental-vm-modules", "-e", CHECK,
                    str(ROOT / "plugins/default-web-apps/assets/dosing_assistant")], check=True)
